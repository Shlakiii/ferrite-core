#include "idt.h"

#include <io/printg.h>

#include <func/../init.h>

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static IDTEntry idt[256];
static IDTPointer idt_ptr;

extern "C" void exception_handler_wrapper();

__asm__(
    ".global exception_handler_wrapper\n"
    "exception_handler_wrapper:\n"
    "   pusha\n"
    "   call exception_handler_impl\n"
    "   popa\n"
    "   iret\n"
);

extern "C" void exception_handler_impl() {
    kernel_panic("System halted to prevent damage.\nThis usually means:\n- Invalid memory access\n- Division by zero\n- Invalid instruction");
}

static void set_idt_entry(int index, uint32_t handler_address) {
    idt[index].offset_low = handler_address & 0xFFFF;
    idt[index].selector = 0x08;
    idt[index].zero = 0;
    idt[index].type_attr = 0x8E;
    idt[index].offset_high = (handler_address >> 16) & 0xFFFF;
}

void init_idt() {
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }
    
    uint32_t handler = (uint32_t)exception_handler_wrapper;
    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, handler);
    }
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;
    
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    
    log(3, "IDT: initialized successfully.");
}
