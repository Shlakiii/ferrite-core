#ifndef FONT_FLOAD_H
#define FONT_FLOAD_H

#include <stdint.h>

typedef uint8_t (*font_decoder_t)(const uint8_t* str, int* offset);

typedef struct {
    const char* name;
    const char* encoding;
    uint16_t char_count;
    uint8_t char_width;
    uint8_t char_height;
} font_info_t;

void fload_init(void);
void fload_load(void);
int fload_set(const char* name);
int fload_set_data(const uint8_t* data);
const uint8_t* fload_get_data(void);
font_info_t fload_get_info(void);
uint8_t fload_decode_char(const uint8_t* str, int* offset);
uint32_t fload_get_count(void);
const char* fload_get_name_index(uint32_t index);

#endif
