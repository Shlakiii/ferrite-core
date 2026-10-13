#include "scrollbuf.h"
#include "vfs.h"

#include <lib/strtools/string.h>

#include <io/printg.h>
#include <io/fb.h>

uint8_t sb_static_buf[SB_MEM_SIZE];

#define FONT_W 8
#define FONT_H 8
#define CELL_W FONT_W
#define CELL_H (FONT_H + 1)

static const uint32_t cga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static uint8_t* buf_base    = NULL;
static int      sb_cols     = SB_COLS;
static int      sb_lines    = SB_LINES;
static int      head        = 0;
static int      tail        = 0;
static int      total_lines = 0;
static int      screen_h    = 0;
static int      view_offset = 0;
static bool     initialized = false;
static bool     dump_enabled = true;

#define WRAP_IDX(idx) (((idx) % sb_lines + sb_lines) % sb_lines)
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define ROW_BYTES     ((size_t)sb_cols * 2)

static inline uint8_t* row_ptr(int idx) {
    return buf_base + (size_t)(((idx % sb_lines) + sb_lines) % sb_lines) * ROW_BYTES;
}

static inline int wrap_idx(int idx) {
    return WRAP_IDX(idx);
}

static inline int row_from_tail(int offset) {
    int idx = tail - 1 - offset;
    while (idx < 0) idx += sb_lines;
    return idx % sb_lines;
}

static void clear_buf_row(int idx) {
    uint8_t* r = row_ptr(idx);
    for (int x = 0; x < sb_cols; x++) {
        r[x * 2]     = ' ';
        r[x * 2 + 1] = 0x07;
    }
}

static void clear_buf_rows_range(int start_tail_idx, int count) {
    for (int i = 0; i < count; i++) {
        int idx = wrap_idx(start_tail_idx + i);
        uint8_t* r = buf_base + (size_t)idx * ROW_BYTES;
        for (int x = 0; x < sb_cols; x++) {
            r[x * 2]     = ' ';
            r[x * 2 + 1] = 0x07;
        }
    }
}

static void render_row(int row_idx, int screen_row) {
    const uint8_t* src = row_ptr(row_idx);
    int py = screen_row * CELL_H;

    for (int x = 0; x < sb_cols; x++) {
        char    c    = (char)src[x * 2];
        uint8_t attr = src[x * 2 + 1];

        uint32_t fg = cga_palette[attr & 0x0F];
        uint32_t bg = cga_palette[(attr >> 4) & 0x0F];
        int px = x * CELL_W;

        fb_draw_rect(px, py, CELL_W, CELL_H, bg);
        fb_draw_char(px, py, c, fg, bg);
    }
}

static void redraw_from_buf(void) {
    for (int row = 0; row < screen_h; row++) {
        int buf_row_offset = screen_h - 1 - row + view_offset;
        if (buf_row_offset >= total_lines) {
            int py = row * CELL_H;
            fb_draw_rect(0, py, sb_cols * CELL_W, CELL_H, 0x000000);
        } else {
            render_row(row_from_tail(buf_row_offset), row);
        }
    }
}

static void draw_scroll_indicator(void) {
    if (view_offset == 0) return;

    char ind[24];
    int  n = 0;
    int  tmp = view_offset;

    char nbuf[12];
    int  ni = 0;
    if (tmp == 0) {
        nbuf[ni++] = '0';
    } else {
        while (tmp > 0 && ni < 11) {
            nbuf[ni++] = '0' + (tmp % 10);
            tmp /= 10;
        }
    }

    ind[n++] = '[';
    ind[n++] = '|';
    ind[n++] = ' ';
    while (ni > 0 && n < 23) ind[n++] = nbuf[--ni];
    ind[n++] = ' ';
    ind[n++] = '^';
    ind[n++] = ']';
    ind[n]   = '\0';

    int start_x = sb_cols - n;
    if (start_x < 0) start_x = 0;

    for (int i = 0; i < n && (start_x + i) < sb_cols; i++) {
        int px = (start_x + i) * CELL_W;
        int py = 0;
        fb_draw_rect(px, py, CELL_W, CELL_H, 0xAA0000);
        fb_draw_char(px, py, ind[i], 0xFFFFFF, 0xAA0000);
    }
}

void sb_init(int screen_width, int screen_height) {
    sb_cols  = (screen_width  > 0 && screen_width  <= SB_COLS) ? screen_width : SB_COLS;
    screen_h = (screen_height > 0) ? screen_height : 25;

    buf_base = sb_static_buf;

    head = 0;
    tail = 0;
    total_lines = 0;
    view_offset = 0;
    initialized = true;
    dump_enabled = true;

    uint8_t* p   = buf_base;
    uint8_t* end = buf_base + SB_MEM_SIZE;
    while (p < end) {
        p[0] = ' ';
        p[1] = 0x07;
        p += 2;
    }
}

void sb_on_char(int x, int y, char c, uint8_t attr) {
    if (!initialized) return;
    if (x < 0 || x >= sb_cols) return;
    if (y < 0 || y >= screen_h) return;

    int base = tail - screen_h;
    int row_idx = ((base + y) % SB_LINES + SB_LINES) % SB_LINES;

    uint8_t* r = row_ptr(row_idx);
    r[x * 2]     = (uint8_t)c;
    r[x * 2 + 1] = attr;
}

void sb_on_scroll(void) {
    sb_on_batch_scroll(screen_h);
}

void sb_on_scroll_line(void) {
    if (!initialized) return;

    clear_buf_row(tail);

    tail = (tail + 1) % SB_LINES;
    if (total_lines < SB_LINES) total_lines++;
    else                          head = (head + 1) % SB_LINES;

    if (view_offset > 0) {
        view_offset++;
        int max_offset = total_lines - screen_h;
        if (max_offset < 0) max_offset = 0;
        if (view_offset > max_offset) view_offset = max_offset;
    }
}

void sb_on_batch_scroll(int lines) {
    if (!initialized || lines <= 0) return;
    if (lines > screen_h) lines = screen_h;

    clear_buf_rows_range(tail, lines);

    for (int i = 0; i < lines; i++) {
        tail = (tail + 1) % SB_LINES;
        if (total_lines < SB_LINES) total_lines++;
        else                         head = (head + 1) % SB_LINES;
    }

    if (view_offset > 0) {
        int max_offset = total_lines - screen_h;
        if (max_offset < 0) max_offset = 0;
        view_offset = MIN(view_offset + lines, max_offset);
    }
}

void sb_on_clear(void) {
    if (!initialized) return;

    clear_buf_rows_range(tail, screen_h);
    for (int i = 0; i < screen_h; i++) {
        tail = (tail + 1) % SB_LINES;
        if (total_lines < SB_LINES) total_lines++;
        else head = (head + 1) % SB_LINES;
    }
    view_offset = 0;
}

bool sb_scroll_up(int lines) {
    if (!initialized || lines <= 0) return false;
    int max_offset = total_lines - screen_h;
    if (max_offset <= 0) return false;

    int new_offset = view_offset + lines;
    if (new_offset > max_offset) new_offset = max_offset;
    if (new_offset == view_offset) return false;

    view_offset = new_offset;
    redraw_from_buf();
    draw_scroll_indicator();
    return true;
}

bool sb_scroll_down(int lines) {
    if (!initialized || view_offset == 0 || lines <= 0) return false;

    view_offset -= lines;
    if (view_offset < 0) view_offset = 0;

    redraw_from_buf();
    if (view_offset > 0) draw_scroll_indicator();
    return true;
}

bool sb_is_scrolled(void) {
    return initialized && (view_offset > 0);
}

void sb_restore(void) {
    if (!initialized || view_offset == 0) return;
    view_offset = 0;
    redraw_from_buf();
}

void sb_set_dump_enabled(bool enabled) {
    dump_enabled = enabled;
}

bool sb_get_dump_enabled(void) {
    return dump_enabled;
}

int sb_get_total_lines(void) {
    return initialized ? total_lines : 0;
}

int sb_get_view_offset(void) {
    return initialized ? view_offset : 0;
}

void sb_invalidate(void) {
    if (!initialized) return;
    redraw_from_buf();
    if (view_offset > 0) draw_scroll_indicator();
}

static int itoa_into(char* buf, int pos, int limit, int val) {
    char nbuf[12];
    int ni = 0;
    if (val == 0) {
        nbuf[ni++] = '0';
    } else {
        int v = val;
        while (v > 0 && ni < 11) { nbuf[ni++] = '0' + (v % 10); v /= 10; }
    }
    while (ni > 0 && pos < limit) buf[pos++] = nbuf[--ni];
    return pos;
}

static int strlit_into(char* buf, int pos, int limit, const char* s) {
    while (*s && pos < limit) buf[pos++] = *s++;
    return pos;
}

void sb_dump_to_fs(void) {
    if (!initialized) return;

    char dump_buf[8192];
    int pos = 0;
    const int dump_limit = (int)sizeof(dump_buf) - 1;

    pos = strlit_into(dump_buf, pos, dump_limit, "=== Scrollback buffer dump ===\n");
    pos = strlit_into(dump_buf, pos, dump_limit, "Lines: ");
    pos = itoa_into  (dump_buf, pos, dump_limit, total_lines);
    pos = strlit_into(dump_buf, pos, dump_limit, " | View: ");
    pos = itoa_into  (dump_buf, pos, dump_limit, view_offset);
    pos = strlit_into(dump_buf, pos, dump_limit, " | Screen: ");
    pos = itoa_into  (dump_buf, pos, dump_limit, sb_cols);
    if (pos < dump_limit) dump_buf[pos++] = 'x';
    pos = itoa_into  (dump_buf, pos, dump_limit, screen_h);
    if (pos < dump_limit) dump_buf[pos++] = '\n';

    int dump_rows = 80;
    if (dump_rows > total_lines) dump_rows = total_lines;

    for (int r = dump_rows - 1; r >= 0; r--) {
        int offset = r + view_offset;
        if (offset >= total_lines) continue;

        int row_idx = row_from_tail(offset);
        uint8_t* row = row_ptr(row_idx);

        int len = sb_cols;
        while (len > 0 && (char)row[(len - 1) * 2] == ' ') len--;

        for (int x = 0; x < len && pos < dump_limit; x++)
            dump_buf[pos++] = (char)row[x * 2];

        if (pos < dump_limit) dump_buf[pos++] = '\n';
    }
    dump_buf[pos] = '\0';

    vfs_write_file("/scrollbuf.log", dump_buf, (uint32_t)pos);
}
