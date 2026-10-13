#include "gdt.h"

#include <io/printg.h>

#define ACCESS_CODE(dpl)  (uint8_t)(0x9A | ((dpl) << 5))
#define ACCESS_DATA(dpl)  (uint8_t)(0x92 | ((dpl) << 5))
#define ACCESS_TSS        (uint8_t)(0x89)

#define GRAN_32BIT  (uint8_t)(0xCF)
#define GRAN_TSS    (uint8_t)(0x00)

static struct GDTEntry   gdt[GDT_ENTRIES];
static struct GDTPointer gdt_ptr;
static struct TSS        kernel_tss;

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t  access, uint8_t  granularity) {
    struct GDTEntry* e = &gdt[idx];

    e->limit_low   = (uint16_t)(limit & 0xFFFF);
    e->base_low    = (uint16_t)(base  & 0xFFFF);
    e->base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    e->access      = access;
    e->granularity = (uint8_t)((granularity & 0xF0) | ((limit >> 16) & 0x0F));
    e->base_high   = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss_entry(int idx, uint32_t base, uint32_t limit) {
    struct GDTEntry* e = &gdt[idx];

    e->limit_low   = (uint16_t)(limit & 0xFFFF);
    e->base_low    = (uint16_t)(base  & 0xFFFF);
    e->base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    e->access      = ACCESS_TSS;
    e->granularity = GRAN_TSS | ((limit >> 16) & 0x0F);
    e->base_high   = (uint8_t)((base  >> 24) & 0xFF);
}

static void gdt_flush(void) {
    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp %1, $.flush_cs\n\t"
        ".flush_cs:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "m"(gdt_ptr),
          "i"(SEG_KERNEL_CODE),
          "i"(SEG_KERNEL_DATA)
        : "eax", "memory"
    );
}

static void tss_flush(void) {
    __asm__ volatile(
        "ltr %0\n\t"
        :
        : "r"((uint16_t)SEG_TSS)
    );
}

static void tss_init(void) {
    uint32_t tss_base  = (uint32_t)&kernel_tss;
    uint32_t tss_limit = (uint32_t)sizeof(kernel_tss) - 1;

    for (uint32_t i = 0; i < sizeof(kernel_tss); ++i)
        ((uint8_t*)&kernel_tss)[i] = 0;

    kernel_tss.ss0        = SEG_KERNEL_DATA;
    kernel_tss.esp0       = 0;
    kernel_tss.ss1        = SEG_FROOT_DATA;
    kernel_tss.esp1       = 0;
    kernel_tss.ss2        = SEG_ROOT_DATA;
    kernel_tss.esp2       = 0;
    kernel_tss.iomap_base = sizeof(kernel_tss);

    gdt_set_tss_entry(GDT_TSS_IDX, tss_base, tss_limit);
}

void init_gdt(void) {
    gdt_set_entry(GDT_NULL_IDX, 0, 0, 0, 0);

    gdt_set_entry(GDT_KERNEL_CODE_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_CODE(PRIV_KERNEL), GRAN_32BIT);
    gdt_set_entry(GDT_KERNEL_DATA_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_DATA(PRIV_KERNEL), GRAN_32BIT);

    gdt_set_entry(GDT_FROOT_CODE_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_CODE(PRIV_FROOT), GRAN_32BIT);
    gdt_set_entry(GDT_FROOT_DATA_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_DATA(PRIV_FROOT), GRAN_32BIT);

    gdt_set_entry(GDT_ROOT_CODE_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_CODE(PRIV_ROOT), GRAN_32BIT);
    gdt_set_entry(GDT_ROOT_DATA_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_DATA(PRIV_ROOT), GRAN_32BIT);

    gdt_set_entry(GDT_USER_CODE_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_CODE(PRIV_USER), GRAN_32BIT);
    gdt_set_entry(GDT_USER_DATA_IDX, 0x00000000, 0xFFFFFFFF, ACCESS_DATA(PRIV_USER), GRAN_32BIT);

    tss_init();

    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint32_t)&gdt;

    gdt_flush();
    tss_flush();

    log(3, "GDT: initialized.");
}

void gdt_set_kernel_stack(uint32_t esp) {
    kernel_tss.esp0 = esp;
}

uint8_t gdt_get_privilege(void) {
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return (uint8_t)(cs & 0x03);
}
