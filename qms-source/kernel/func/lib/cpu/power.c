#include <io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

void reboot(void) {
    __asm__ __volatile__ ("cli");
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) {
        __asm__ __volatile__ ("hlt");
    }
}

void shutdown(void) {
    __asm__ __volatile__ ("cli");
    outw(0x604,  0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    while (1) {
        __asm__ __volatile__ ("hlt");
    }
}

#ifdef __cplusplus
} 
#endif

