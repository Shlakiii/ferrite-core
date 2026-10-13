#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ( "inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__ ( "outb %1, %0" : : "dN"(port), "a"(value));
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__ ( "inw %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

void outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__ ( "outw %1, %0" : : "dN"(port), "a"(value));
}

void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__ ("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__ ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#ifdef __cplusplus
} 
#endif
