#ifndef FB_H
#define FB_H

#include <stdint.h>

struct fb_info {
    uint32_t* address;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
};

extern struct fb_info kernel_fb;

#ifdef __cplusplus
extern "C" {
#endif

void fb_init(uint32_t mb_addr);
void fb_putpixel(int x, int y, uint32_t color);
void fb_clear(uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_image(int x, int y, uint32_t w, uint32_t h, uint32_t* data);

#ifdef __cplusplus
}
#endif

#endif
