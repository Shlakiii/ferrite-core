#include "kkd_ps2_standard.h"

#include <io/io.h>
#include <io/printg.h>

#include <cstddef>

static const char* name = "PS/2 Standard Desktop (101/104-key)";

static const char normal_map[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char shift_map[128] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static bool shift_left = false;
static bool shift_right = false;

static bool is_shift_pressed(void) {
    return shift_left || shift_right;
}

bool kkd_ps2_standard_init(void) {
    outb(0x60, 0xF0);
    outb(0x60, 0x02);
    log(3, "[KKD] %s initialized", name);
    return true;
}

bool kkd_ps2_standard_get_event(KeyboardEvent* ev) {
    if (!(inb(0x64) & 1)) return false;

    uint8_t sc = inb(0x60);
    bool pressed = (sc & 0x80) == 0;
    sc &= 0x7F;

    if (sc == 0x2A) { shift_left  = pressed; }
    if (sc == 0x36) { shift_right = pressed; }

    ev->pressed   = pressed;
    ev->keycode   = sc;
    ev->modifiers = 0;
    if (is_shift_pressed()) ev->modifiers |= KBD_MOD_SHIFT;

    ev->ascii = is_shift_pressed() ? shift_map[sc] : normal_map[sc];

    return true;
}

KkdDriver kkd_ps2_standard_driver = {
    .name      = "PS/2 Standard Desktop",
    .type      = KKD_PS2_STANDARD,
    .init      = kkd_ps2_standard_init,
    .get_event = kkd_ps2_standard_get_event,
    .set_leds  = NULL
};
