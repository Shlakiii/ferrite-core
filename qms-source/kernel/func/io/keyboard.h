#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KBD_MOD_SHIFT   0x01
#define KBD_MOD_CTRL    0x02
#define KBD_MOD_ALT     0x04

uint8_t kbd_get_modifiers(void);
uint8_t get_special_key(void);
char    get_char(void);

typedef enum {
    KKD_PS2_STANDARD = 0,
    KKD_LAPTOP_GENERIC,
    KKD_NONE
} KkdKeyboardType;

typedef struct {
    uint16_t keycode;
    uint8_t  modifiers;
    bool     pressed;
    char     ascii;
} KeyboardEvent;

typedef struct KkdDriver {
    const char* name;
    KkdKeyboardType type;
    bool (*init)(void);
    bool (*get_event)(KeyboardEvent* ev);
    void (*set_leds)(uint8_t leds);
} KkdDriver;

void keyboard_init(void);
bool keyboard_get_event(KeyboardEvent* ev);
KkdKeyboardType keyboard_get_current_type(void);
const char* keyboard_get_current_name(void);

#endif
