#include "keyboard.h"

#include <lib/strtools/string.h>

#include <io/io.h>
#include <io/printg.h>

extern KkdDriver kkd_ps2_standard_driver;
extern KkdDriver kkd_laptop_generic_driver;

static KkdDriver* registered[8] = {0};
static int reg_count = 0;
static KkdDriver* active_driver = NULL;
static KkdKeyboardType current_type = KKD_NONE;

static uint8_t current_mods = 0;
static uint8_t last_special = 0;

uint8_t kbd_get_modifiers(void) { return current_mods; }

uint8_t get_special_key(void) {
    uint8_t t = last_special;
    last_special = 0;
    return t;
}

char get_char(void) {
    KeyboardEvent ev = {0};
    if (keyboard_get_event(&ev) && ev.pressed && ev.ascii)
        return ev.ascii;
    return 0;
}

bool keyboard_register_driver(KkdDriver* drv) {
    if (!drv || reg_count >= 8) return false;
    registered[reg_count++] = drv;
    return true;
}

static uint16_t ps2_read_id(void) {
    outb(0x60, 0xF2);
    for (volatile int i = 0; i < 500000; i++);

    uint8_t b1 = 0, b2 = 0;
    if (inb(0x64) & 1) b1 = inb(0x60);
    if (inb(0x64) & 1) b1 = inb(0x60);
    if (inb(0x64) & 1) b2 = inb(0x60);

    if (b1 == 0xAB) return (0xAB00 | b2);
    return 0;
}

void keyboard_init(void) {
    log(3, "KBD: Keyboard autodetection...");

    keyboard_register_driver(&kkd_ps2_standard_driver);
    keyboard_register_driver(&kkd_laptop_generic_driver);

    uint16_t id = ps2_read_id();

    KkdKeyboardType chosen = KKD_PS2_STANDARD;

    if (id == 0xAB83 || id == 0xAB84) {
        chosen = KKD_PS2_STANDARD;
        log(3, "KBD: Detected: Standard Desktop PS/2");
    } else if (id == 0xABC1 || id == 0xABC2 || id == 0xAB41 || id == 0xAB54) {
        chosen = KKD_LAPTOP_GENERIC;
        log(3, "KBD: Detected: Laptop PS/2 keyboard");
    } else {
        log(1, "KBD: Unknown PS/2 ID, using Standard Desktop fallback");
    }

    for (int i = 0; i < reg_count; i++) {
        if (registered[i]->type == chosen) {
            active_driver = registered[i];
            if (active_driver->init()) {
                current_type = chosen;
                return;
            }
        }
    }
    log(1, "KBD: Failed to activate any driver.");
}

bool keyboard_get_event(KeyboardEvent* ev) {
    if (!active_driver || !active_driver->get_event) return false;
    bool res = active_driver->get_event(ev);
    if (res) {
        current_mods = ev->modifiers;
        if (ev->pressed && ev->keycode >= 0xE000)
            last_special = (uint8_t)(ev->keycode & 0xFF);
    }
    return res;
}

KkdKeyboardType keyboard_get_current_type(void) { return current_type; }
const char* keyboard_get_current_name(void) {
    return active_driver ? active_driver->name : "None";
}
