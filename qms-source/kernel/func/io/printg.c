#include "printg.h"
#include "fb.h"

#include <func/scrollbuf.h>

#include <lib/strtools/string.h>
#include <lib/font/fload.h>

#include <data/logo.h>

/*
	0  черный
	1  синий
	2  зелёный
	3  голубой
	4  красный
	5  пурпурный
	6  коричневый
	7  светло серый
	8  темно серый
	9  ярко синий
	10 ярко зеленый
	11 ярко голубой
	12 ярко красный
	13 ярко пурпурный
	14 жёлтый
	15 белый
*/

static const uint32_t cga_palette[16] = {
    0x000000,
    0x0000AA,
    0x00AA00,
    0x00AAAA,
    0xAA0000,
    0xAA00AA,
    0xAA5500,
    0xAAAAAA,
    0x555555,
    0x5555FF,
    0x55FF55,
    0x55FFFF,
    0xFF5555,
    0xFF55FF,
    0xFFFF55,
    0xFFFFFF,
};

#define FONT_W 8
#define FONT_H 8

#define CELL_W FONT_W
#define CELL_H (FONT_H + 1)

struct SnprintfCtx {
    char*   buf;
    size_t  size;
    size_t  pos;
};

int      cursor_x = 0, cursor_y = 0;
uint16_t sw = 80, sh = 50;
bool     enable_border_cursor = false;


static uint8_t  curr_txt = 0x7;
static uint8_t  curr_bg  = 0x0;
static int      last_frame_x = 0, last_frame_y = 0;

static inline uint32_t attr_fg(uint8_t attr) {
    return cga_palette[attr & 0x0F];
}
static inline uint32_t attr_bg(uint8_t attr) {
    return cga_palette[(attr >> 4) & 0x0F];
}

static void fb_draw_cell(int cx, int cy, char c, uint8_t txt, uint8_t bg) {
    int px = cx * CELL_W;
    int py = cy * CELL_H;
    fb_draw_rect(px, py, CELL_W, CELL_H, cga_palette[bg & 0x0F]);
    fb_draw_char(px, py, c, cga_palette[txt & 0x0F], cga_palette[bg & 0x0F]);
}

void init_printg() {
    if (kernel_fb.width > 0 && kernel_fb.height > 0) {
        sw = (uint16_t)(kernel_fb.width  / CELL_W);
        sh = (uint16_t)(kernel_fb.height / CELL_H);
    } else {
        sw = 80;
        sh = 50;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void print_raw(char c, uint8_t txt, uint8_t bg) {
    if (c == '\n') {
        for (int x = cursor_x; x < sw; x++) {
            fb_draw_cell(x, cursor_y, ' ', bg, bg);
        }
        cursor_x = 0;
        cursor_y++;
    } else {
        fb_draw_cell(cursor_x, cursor_y, c, txt, bg);

        uint8_t attr = (uint8_t)((bg << 4) | (txt & 0x0F));
        sb_on_char(cursor_x, cursor_y, c, attr);

        cursor_x++;
        if (cursor_x >= sw) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    if (cursor_y >= sh) scroll();
}

static void pg_print_num(int n, int base, uint8_t txt, uint8_t bg) {
    if (n == 0) { print_raw('0', txt, bg); return; }

    char buf[32];
    int  i   = 0;
    bool neg = false;

    if (n < 0 && base == 10) { neg = true; n = -n; }

    while (n > 0) {
        int rem = n % base;
        buf[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
        n /= base;
    }
    if (neg) print_raw('-', txt, bg);
    while (i > 0) print_raw(buf[--i], txt, bg);
}

static void pg_print_uint(unsigned int n, int base, uint8_t txt, uint8_t bg) {
    if (n == 0) { print_raw('0', txt, bg); return; }
    char buf[32]; int i = 0;
    while (n > 0) {
        unsigned int rem = n % (unsigned int)base;
        buf[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
        n /= (unsigned int)base;
    }
    while (i > 0) print_raw(buf[--i], txt, bg);
}

void print_num_ex(int n, int base, uint8_t t, uint8_t b) {
    if (base == 10) {
        pg_print_num(n, 10, t, b);
    } else {
        pg_print_uint((unsigned int)n, base, t, b);
    }
}

static void vprintg_core(int t, int b, const char* fmt, va_list args) {
    uint8_t txt = (t == -1) ? curr_txt : (uint8_t)t;
    uint8_t bg  = (b == -1) ? curr_bg  : (uint8_t)b;

    bool no_nl = false;
    {
        const char* scan = fmt;
        while (*scan) {
            if ((uint8_t)*scan == 0x01) { no_nl = true; break; }
            scan++;
        }
    }

    const uint8_t* p = (const uint8_t*)fmt;
    while (*p) {
        if (*p == 0x01) { p++; continue; }

        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s) {
                        int off = 0;
                        char ch = fload_decode_char((const uint8_t*)s, &off);
                        print_raw(ch, txt, bg);
                        s += off;
                    }
                    break;
                }
                case 'd':
                    pg_print_num(va_arg(args, int), 10, txt, bg);
                    break;
                case 'u':
                    pg_print_uint(va_arg(args, unsigned int), 10, txt, bg);
                    break;
                case 'x':
                    pg_print_uint(va_arg(args, unsigned int), 16, txt, bg);
                    break;
                case 'c':
                    print_raw((char)va_arg(args, int), txt, bg);
                    break;
                case '%':
                    print_raw('%', txt, bg);
                    break;
                default:
                    print_raw('%',  txt, bg);
                    print_raw(*p,   txt, bg);
                    break;
            }
            p++;
        } else {
            int off = 0;
            char ch = fload_decode_char(p, &off);
            print_raw(ch, txt, bg);
            p += off;
        }
    }

    if (!no_nl) print_raw('\n', txt, bg);
    if (enable_border_cursor) update_cursor_frame();
}

void printg(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintg_core(-1, -1, fmt, args);
    va_end(args);
}

void printgc(int t, int b, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintg_core(t, b, fmt, args);
    va_end(args);
}

void log(int level, const char* fmt, ...) {
    switch (level) {
        case 1: printgc(4,  0, "[E] " PG_NNL); break;
        case 2: printgc(14, 0, "[W] " PG_NNL); break;
        case 3: printgc(1,  0, "[I] " PG_NNL); break;
        case 4: printgc(15, 0, "[D] " PG_NNL); break;
        default: break;
    }

    va_list args;
    va_start(args, fmt);
    vprintg_core(-1, -1, fmt, args);
    va_end(args);
}

void scroll() {
    int tsh = u16int(sh);
    sb_on_batch_scroll(tsh);

    uint8_t* fb = (uint8_t*)kernel_fb.address;
    uint32_t line_bytes = kernel_fb.pitch * CELL_H;
    uint32_t total      = kernel_fb.pitch * kernel_fb.height;
    uint32_t move_bytes = total - line_bytes;

    uint32_t* dst32 = (uint32_t*)fb;
    uint32_t* src32 = (uint32_t*)(fb + line_bytes);
    uint32_t  words = move_bytes / 4;
    for (uint32_t i = 0; i < words; i++) dst32[i] = src32[i];

    uint32_t bg_color  = cga_palette[curr_bg & 0x0F];
    uint32_t tail_bytes = kernel_fb.pitch * CELL_H;
    uint8_t* last_row  = fb + (total - tail_bytes);
    for (uint32_t i = 0; i < tail_bytes / 4; i++) {
        ((uint32_t*)last_row)[i] = bg_color;
    }

    cursor_y = sh - 1;
}

void clear_screen() {
    sb_on_clear();
    fb_clear(cga_palette[curr_bg & 0x0F]);
    cursor_x = 0;
    cursor_y = 0;
}

void clear_to_eol(int x, int y) {
    for (int i = x; i < sw; i++) {
        fb_draw_cell(i, y, ' ', curr_txt, curr_bg);
    }
}

void goto_xy(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void cursor_back() {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = sw - 1;
    }
}

void update_cursor_frame() {
    if (!enable_border_cursor) return;
    fb_draw_cell(last_frame_x, last_frame_y, ' ', curr_txt, curr_bg);
    fb_draw_rect(cursor_x * FONT_W, cursor_y * FONT_H, FONT_W, FONT_H, cga_palette[curr_txt & 0x0F]);
    last_frame_x = cursor_x;
    last_frame_y = cursor_y;
}

static void snp_putc(struct SnprintfCtx* ctx, char c) {
    if (ctx->pos < ctx->size - 1) {
        ctx->buf[ctx->pos++] = c;
    }
}

static void snp_puts(struct SnprintfCtx* ctx, const char* s) {
    while (*s) snp_putc(ctx, *s++);
}

static void snp_print_num(struct SnprintfCtx* ctx, int n, int base) {
    if (n == 0) { snp_putc(ctx, '0'); return; }
    char buf[32];
    int i = 0;
    bool neg = false;
    if (n < 0 && base == 10) { neg = true; n = -n; }
    while (n > 0) {
        int rem = n % base;
        buf[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
        n /= base;
    }
    if (neg) snp_putc(ctx, '-');
    while (i > 0) snp_putc(ctx, buf[--i]);
}

static void snp_print_uint(struct SnprintfCtx* ctx, unsigned int n, int base, bool upper) {
    if (n == 0) { snp_putc(ctx, '0'); return; }
    char buf[32];
    int i = 0;
    while (n > 0) {
        unsigned int rem = n % (unsigned int)base;
        buf[i++] = (rem < 10) ? ('0' + rem) : ((upper ? 'A' : 'a') + rem - 10);
        n /= (unsigned int)base;
    }
    while (i > 0) snp_putc(ctx, buf[--i]);
}

static int vsnprintf_core(struct SnprintfCtx* ctx, const char* fmt, va_list args) {
    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            snp_putc(ctx, *p++);
            continue;
        }
        p++;
        int width = 0;
        char pad = ' ';
        bool sign_always = false;

        if (*p == '+') { sign_always = true; p++; }
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        switch (*p) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                int len = strlen(s);
                int pad_count = width - len;
                if (pad == ' ' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, ' ');
                while (*s) snp_putc(ctx, *s++);
                break;
            }
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                char tmp[32];
                struct SnprintfCtx tmp_ctx = { tmp, sizeof(tmp), 0 };
                snp_print_num(&tmp_ctx, val, 10);
                tmp_ctx.buf[tmp_ctx.pos] = '\0';
                int len = tmp_ctx.pos;
                int pad_count = width - len;
                if (pad == ' ' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, ' ');
                snp_puts(ctx, tmp);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[32];
                struct SnprintfCtx tmp_ctx = { tmp, sizeof(tmp), 0 };
                snp_print_uint(&tmp_ctx, val, 10, false);
                tmp_ctx.buf[tmp_ctx.pos] = '\0';
                int len = tmp_ctx.pos;
                int pad_count = width - len;
                if (pad == ' ' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, ' ');
                snp_puts(ctx, tmp);
                break;
            }
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[32];
                struct SnprintfCtx tmp_ctx = { tmp, sizeof(tmp), 0 };
                snp_print_uint(&tmp_ctx, val, 16, false);
                tmp_ctx.buf[tmp_ctx.pos] = '\0';
                int len = tmp_ctx.pos;
                int pad_count = width - len;
                if (pad == '0' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, '0');
                else if (pad == ' ' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, ' ');
                snp_puts(ctx, tmp);
                break;
            }
            case 'X': {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[32];
                struct SnprintfCtx tmp_ctx = { tmp, sizeof(tmp), 0 };
                snp_print_uint(&tmp_ctx, val, 16, true);
                tmp_ctx.buf[tmp_ctx.pos] = '\0';
                int len = tmp_ctx.pos;
                int pad_count = width - len;
                if (pad == '0' && pad_count > 0)
                    while (pad_count-- > 0) snp_putc(ctx, '0');
                snp_puts(ctx, tmp);
                break;
            }
            case 'p': {
                snp_puts(ctx, "0x");
                unsigned int val = (unsigned int)va_arg(args, void*);
                char tmp[32];
                struct SnprintfCtx tmp_ctx = { tmp, sizeof(tmp), 0 };
                snp_print_uint(&tmp_ctx, val, 16, false);
                tmp_ctx.buf[tmp_ctx.pos] = '\0';
                int len = tmp_ctx.pos;
                for (int i = 0; i < 8 - len; i++) snp_putc(ctx, '0');
                snp_puts(ctx, tmp);
                break;
            }
            case 'c': {
                int ch = va_arg(args, int);
                snp_putc(ctx, (char)ch);
                break;
            }
            case '%':
                snp_putc(ctx, '%');
                break;
            default:
                snp_putc(ctx, '%');
                snp_putc(ctx, *p);
                break;
        }
        if (*p) p++;
    }
    return (int)ctx->pos;
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    if (!buf || size == 0) {
        struct SnprintfCtx ctx = { nullptr, 0, 0 };
        return vsnprintf_core(&ctx, fmt, args);
    }
    struct SnprintfCtx ctx = { buf, size, 0 };
    int ret = vsnprintf_core(&ctx, fmt, args);
    buf[ctx.pos < size ? ctx.pos : size - 1] = '\0';
    return ret;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

void show_banner() {
    printgc(3, 0, " _______ _______ _______    _______ _______ ");
    printgc(3, 0, "|       |   |   |    ___|  |       |    ___|");
    printgc(3, 0, "|   |   |       |       |  |   |   |       |");
    printgc(3, 0, "|      _|       |___    |  |   |   |___    |");
    printgc(3, 0, "|_______|__|_|__|_______|  |_______|_______|");
    printg(" ");
}

void show_gbanner(int x, int y) {
    printg("\n\n\n\n"); 

    int cx = x * 8;
    int cy = (cursor_y - 5) * 9; 
    if (cy < 0) cy = 0;

    fb_draw_image(cx, cy, logo_width, logo_height, logo_data);
}
void scbanner(int x, int y, uint32_t* pixels, uint32_t width, uint32_t height, int ns){
    printg("\n");

    int cx = x * 8;
    int cy = (cursor_y -5) * ns;
    if (cy < 0) cy = 0;

    fb_draw_image(cx, cy, width, height, pixels);
}

int tscreen_h() {
	int tsh = u16int(sh);
	return tsh;
}

int tscreen_w() {
	int tsw = u16int(sw);
	return tsw;
}

int tcursor_x() {
	return cursor_x;
}

int tcursor_y() {
	return cursor_y;
}
