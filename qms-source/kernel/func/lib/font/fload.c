#include "ascii.h"
#include "cp866.h"
#include "fload.h"
#include "../../io/io.h"
#include <stddef.h>

#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF

typedef struct {
    const char* name;
    const char* encoding;
    const uint8_t* data;
    uint16_t char_count;
    uint8_t char_width;
    uint8_t char_height;
    font_decoder_t decoder;
} font_descriptor_t;

static const font_descriptor_t* current_font_desc = NULL;

void fload_load(void) {
    if (!current_font_desc || !current_font_desc->data) return;

    uint8_t* font_memory = (uint8_t*)0xA0000;

    outb(VGA_SEQ_INDEX, 0x02); uint8_t seq2 = inb(VGA_SEQ_DATA);
    outb(VGA_SEQ_INDEX, 0x04); uint8_t seq4 = inb(VGA_SEQ_DATA);
    outb(VGA_GC_INDEX, 0x04);  uint8_t gc4  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX, 0x05);  uint8_t gc5  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX, 0x06);  uint8_t gc6  = inb(VGA_GC_DATA);
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x04);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x07);
    outb(VGA_GC_INDEX, 0x04);  outb(VGA_GC_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x05);  outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x06);  outb(VGA_GC_DATA, 0x00);

    uint32_t count = current_font_desc->char_count;
    uint8_t h = current_font_desc->char_height;    
    const uint8_t* data = current_font_desc->data; 

    for (uint32_t i = 0; i < count; i++) {
        for (uint8_t j = 0; j < h; j++) {
            font_memory[i * 32 + j] = data[i * h + j];
        }
        for (uint8_t j = h; j < 32; j++) {
            font_memory[i * 32 + j] = 0;
        }
    }
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, seq2);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, seq4);
    outb(VGA_GC_INDEX, 0x04);  outb(VGA_GC_DATA, gc4);
    outb(VGA_GC_INDEX, 0x05);  outb(VGA_GC_DATA, gc5);
    outb(VGA_GC_INDEX, 0x06);  outb(VGA_GC_DATA, gc6);
}

static uint8_t decode_ascii(const uint8_t* str, int* offset) {
    uint8_t c = str[0];
    *offset = 1;
    if (c >= 128) {
        return '?';
    }
    return c;
}

static uint8_t decode_cp866(const uint8_t* str, int* offset) {
    uint8_t c = str[0];

    if (c < 0x80) {
        *offset = 1;
        return c;
    }

    if ((c & 0xE0) == 0xC0 && str[1] != 0) {
        uint8_t c1 = str[0];
        uint8_t c2 = str[1];
        
        int unicode = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
        
        *offset = 2;
        
        if (unicode >= 0x0410 && unicode <= 0x041F) {
            return 0x80 + (unicode - 0x0410);
        }
        if (unicode >= 0x0420 && unicode <= 0x042F) {
            return 0x90 + (unicode - 0x0420);
        }
        if (unicode >= 0x0430 && unicode <= 0x043F) {
            return 0xA0 + (unicode - 0x0430);
        }
        if (unicode >= 0x0440 && unicode <= 0x044F) {
            return 0xE0 + (unicode - 0x0440);
        }
        if (unicode == 0x0401) {
            return 0xF0;
        }
        if (unicode == 0x0451) {
            return 0xF1;
        }
        return '?';
    }
    *offset = 1;
    return '?';
}

static const font_descriptor_t font_registry[] = {
    {
        .name = "ascii",
        .encoding = "ascii",
        .data = font_ascii,
        .char_count = 128,
        .char_width = 8,
        .char_height = 8,
        .decoder = decode_ascii
    },
    {
        .name = "cp866",
        .encoding = "cp866",
        .data = font_cp866,
        .char_count = 256,
        .char_width = 8,
        .char_height = 8,
        .decoder = decode_cp866
    }
};

#define FONT_COUNT (sizeof(font_registry) / sizeof(font_descriptor_t))

void fload_init(void) {
    current_font_desc = &font_registry[1];
    fload_load();
}

int fload_set(const char* name) {
    for (uint32_t i = 0; i < FONT_COUNT; i++) {

        const char* a = font_registry[i].name;
        const char* b = name;
        
        int match = 1;
        while (*a && *b) {
            if (*a != *b) {
                match = 0;
                break;
            }
            a++;
            b++;
        }
        
        if (match && *a == *b) {
            current_font_desc = &font_registry[i];
            return 1;
        }
    }
    return 0;
}

int fload_set_data(const uint8_t* data) {
    for (uint32_t i = 0; i < FONT_COUNT; i++) {
        if (font_registry[i].data == data) {
            current_font_desc = &font_registry[i];
            return 1;
        }
    }
    return 0;
}

const uint8_t* fload_get_data(void) {
    if (!current_font_desc) return font_cp866;
    return current_font_desc->data;
}

font_info_t fload_get_info(void) {
    font_info_t info;
    
    if (!current_font_desc) {
        info.name = "cp866";
        info.encoding = "cp866";
        info.char_count = 256;
        info.char_width = 8;
        info.char_height = 8;
        return info;
    }
    
    info.name = current_font_desc->name;
    info.encoding = current_font_desc->encoding;
    info.char_count = current_font_desc->char_count;
    info.char_width = current_font_desc->char_width;
    info.char_height = current_font_desc->char_height;
    
    return info;
}

uint8_t fload_decode_char(const uint8_t* str, int* offset) {
    if (!current_font_desc || !current_font_desc->decoder) {
        return decode_cp866(str, offset);
    }
    return current_font_desc->decoder(str, offset);
}

uint32_t fload_get_count(void) {
    return FONT_COUNT;
}

const char* fload_get_name_index(uint32_t index) {
    if (index >= FONT_COUNT) return NULL;
    return font_registry[index].name;
}
