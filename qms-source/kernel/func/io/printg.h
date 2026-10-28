#ifndef _FUNC_PRINTG_H
#define _FUNC_PRINTG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#define PG_NNL "\x01"

extern int  cursor_x;
extern int  cursor_y;
extern uint16_t sh;
extern uint16_t sw;
extern bool enable_border_cursor;

#ifdef __cplusplus
extern "C" {
#endif

int tscreen_h();
int tscreen_w();
int tcursor_x();
int tcursor_y();

void init_printg();
void print_raw(char c, uint8_t txt, uint8_t bg);
void printg(const char* fmt, ...);
void printgc(int t, int b, const char* fmt, ...);
void print_num_ex(int n, int base, uint8_t t, uint8_t b);
void log(int level, const char* fmt, ...);

void scroll();
void clear_screen();
void clear_to_eol(int x, int y);
void goto_xy (int x, int y);
void cursor_back();
void update_cursor_frame();
void show_banner();
void show_gbanner(int x, int y);

#ifdef _cplusplus
void scbanner(int x, int y, uint32_t* pixels, uint32_t width, uint32_t height, int ns=1);
#else
void scbanner(int x, int y, uint32_t* pixels, uint32_t width, uint32_t height, int ns);
#endif

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int snprintf(char* buf, size_t size, const char* fmt, ...);

#define print_string(s)    printg("%s" PG_NNL, s)
#define print_string_ln(s) printg("%s", s)
#define print_int(i)       printg("%d" PG_NNL, i)
#define print_uint(u)      printg("%u" PG_NNL, u)
#define print_hex(h)       printg("%x" PG_NNL, h)
#define print_char(c)      printg("%c" PG_NNL, c)

#ifdef __cplusplus
}
#endif

#endif
