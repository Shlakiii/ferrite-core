#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDT_NULL_IDX        0
#define GDT_KERNEL_CODE_IDX 1
#define GDT_KERNEL_DATA_IDX 2
#define GDT_FROOT_CODE_IDX  3
#define GDT_FROOT_DATA_IDX  4
#define GDT_ROOT_CODE_IDX   5
#define GDT_ROOT_DATA_IDX   6
#define GDT_USER_CODE_IDX   7
#define GDT_USER_DATA_IDX   8
#define GDT_TSS_IDX         9
#define GDT_ENTRIES         10

#define SEG_KERNEL_CODE  0x08
#define SEG_KERNEL_DATA  0x10
#define SEG_FROOT_CODE   0x1B
#define SEG_FROOT_DATA   0x23
#define SEG_ROOT_CODE    0x2B
#define SEG_ROOT_DATA    0x33
#define SEG_USER_CODE    0x3B
#define SEG_USER_DATA    0x43
#define SEG_TSS          0x48

#define PRIV_KERNEL  0
#define PRIV_FROOT   1
#define PRIV_ROOT    2
#define PRIV_USER    3

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTPointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct TSS {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

void init_gdt(void);
void gdt_set_kernel_stack(uint32_t esp);

uint8_t gdt_get_privilege(void);

#ifdef __cplusplus
}
#endif

#endif
