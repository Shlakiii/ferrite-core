#include "fb.h"
#include <lib/multiboot.h>
#include <lib/font/fload.h>
#include <lib/font/cp866.h>

#define GLYPH_W 8
#define GLYPH_H 8

struct fb_info kernel_fb;

void fb_init(uint32_t mb_addr) {
    struct multiboot_info* mbi = (struct multiboot_info*)mb_addr;

    if (mbi->flags & (1 << 12)) {
        kernel_fb.address = (uint32_t*)(uintptr_t)mbi->fb_addr;
        kernel_fb.width   = mbi->fb_width;
        kernel_fb.height  = mbi->fb_height;
        kernel_fb.pitch   = mbi->fb_pitch;
        kernel_fb.bpp     = mbi->fb_bpp;
    }
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= kernel_fb.width || (uint32_t)y >= kernel_fb.height) return;
    uint32_t* pixel = (uint32_t*)((uint8_t*)kernel_fb.address + (uint32_t)y * kernel_fb.pitch + (uint32_t)x * 4);
    *pixel = color;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < kernel_fb.height; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)kernel_fb.address + y * kernel_fb.pitch);
        for (uint32_t x = 0; x < kernel_fb.width; x++) {
            row[x] = color;
        }
    }
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;

    int x0 = x, y0 = y;
    int x1 = x + w, y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)kernel_fb.width)  x1 = (int)kernel_fb.width;
    if (y1 > (int)kernel_fb.height) y1 = (int)kernel_fb.height;

    for (int py = y0; py < y1; py++) {
        uint32_t* row = (uint32_t*)((uint8_t*)kernel_fb.address + (uint32_t)py * kernel_fb.pitch);
        for (int px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t* font  = fload_get_data();
    uint8_t        idx   = (uint8_t)c;
    const uint8_t* glyph = font + (uint32_t)idx * GLYPH_H;

    {
        int py = y;
        if (py >= 0 && (uint32_t)py < kernel_fb.height) {
            uint32_t* scanline = (uint32_t*)((uint8_t*)kernel_fb.address + (uint32_t)py * kernel_fb.pitch);
            for (int col = 0; col < GLYPH_W; col++) {
                int px = x + col;
                if (px >= 0 && (uint32_t)px < kernel_fb.width)
                    scanline[px] = bg;
            }
        }
    }

    for (int row = 0; row < GLYPH_H; row++) {
        uint8_t bits = glyph[row];
        int     py   = y + 1 + row;
        if (py < 0 || (uint32_t)py >= kernel_fb.height) continue;

        uint32_t* scanline = (uint32_t*)((uint8_t*)kernel_fb.address + (uint32_t)py * kernel_fb.pitch);
        for (int col = 0; col < GLYPH_W; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= kernel_fb.width) continue;
            scanline[px] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void fb_draw_image(int x, int y, uint32_t w, uint32_t h, uint32_t* data) {
    for (uint32_t py = 0; py < h; py++) {
        for (uint32_t px = 0; px < w; px++) {
            uint32_t color = data[py * w + px];

            if ((color >> 24) > 0) {
                fb_putpixel(x + px, y + py, color);
            }
        }
    }
}
