#ifndef _SCROLLBUF_H
#define _SCROLLBUF_H

#include <io/printg.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SB_COLS      85
#define SB_LINES     3200
#define SB_ROW_BYTES (SB_COLS * 2)
#define SB_MEM_SIZE  (SB_LINES * SB_ROW_BYTES)

extern uint8_t sb_static_buf[SB_MEM_SIZE];

#ifdef __cplusplus
extern "C" {
#endif

void sb_init(int screen_width, int screen_height);

void sb_on_char  (int x, int y, char c, uint8_t attr);
void sb_on_scroll();
void sb_on_clear ();

bool sb_scroll_up  (int lines);
bool sb_scroll_down(int lines);
bool sb_is_scrolled();
void sb_restore    ();

void sb_dump_to_fs();

void sb_on_scroll_line(void);
void sb_on_batch_scroll(int lines);

void sb_set_dump_enabled(bool enabled);
bool sb_get_dump_enabled(void);

int  sb_get_total_lines(void);
int  sb_get_view_offset(void);
void sb_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
