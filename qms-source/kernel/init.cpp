#include "init.h"
#include "bin/shell.h"

#include <func/vfs.h>
#include <func/scrollbuf.h>

#include <data/logo.h>

#include <lib/strtools/string.h>
#include <lib/font/fload.h>
#include <lib/metad.h>
#include <lib/cpu/idt.h>
#include <lib/cpu/cpuid.h>
#include <lib/cpu/gdt.h>
#include <lib/cpu/power.h>
#include <lib/rmem/memory.h>
#include <lib/multiboot.h>
#include <lib/fs/qfs/qfs.h>
#include <lib/fs/fat32/vfs_fat32.h>
#include <lib/fs/fat16/vfs_fat16.h>

#include <io/serial.h>
#include <io/printg.h>
#include <io/keyboard.h>
#include <io/fb.h>

#include <test/test_prgs.h>

#include <drivers/hw/ata_pio.h>
#include <drivers/hw/ata_diskio.h>
#include <drivers/pci/pci.h>
#include <drivers/audio/pcs/pcs.h>

#include <func/ferrc/setup/setup.h>

#include <func/lua/lua_runtime.h>

uint32_t multiboot_magic;
uint32_t multiboot_addr;

__asm__ (
    ".section .multiboot\n"
    ".align 4\n"
    ".long 0x1BADB002\n"
    ".long 0x00000007\n"
    ".long -(0x1BADB002 + 0x00000007)\n"
    ".long 0\n"
    ".long 0\n"
    ".long 0\n"
    ".long 0\n"
    ".long 0\n"
    ".long 0\n"
    ".long 800\n"
    ".long 600\n"
    ".long 32\n"

    ".section .text\n"
    ".global _start\n"
    "_start:\n"
    "mov %eax, multiboot_magic\n"
    "mov %ebx, multiboot_addr\n"
    "push %ebp\n"
    "mov %esp, %ebp\n"
    "call kernel_main\n"
    "call out_handler\n"
    "hlt\n"
);

extern "C" { void show_banner(); }
extern "C" char end;

MemoryManager* kernel_mem_manager = nullptr;
MemoryManager* user_mem_manager   = nullptr;
MemoryManager* lua_mem_manager    = nullptr;

alignas(MemoryManager) char km_obj[sizeof(MemoryManager)];
alignas(MemoryManager) char um_obj[sizeof(MemoryManager)];
alignas(MemoryManager) char lm_obj[sizeof(MemoryManager)];

void*  ramfs_primary_addr = nullptr;
size_t ramfs_primary_size = 0;

alignas(AtaDiskIO) static char ata_disk0_buf[sizeof(AtaDiskIO)];
static AtaDiskIO* ata_disk0 = nullptr;

extern "C" {
    void* __dso_handle = 0;
    int __cxa_atexit(void (*f)(void *), void *objptr, void *dso) { return 0; }
    void __cxa_pure_virtual() {}
}

void kernel_panic(const char* msg) {
    printgc(15, 4, " KERNEL PANIC ");
    printgc(15, 4, "%s", msg);
    printgc(15, 4, "System halted.");
    otval_test();
    while (1) { __asm__ __volatile__("hlt"); }
}

static void init_fpu_sse() {
    if (!cpuid_is_supported())
        kernel_panic("CPU: CPUID is required for LuaJIT.");

    cpuid_result_t features;
    cpu_id(1, 0, &features);
    if (!(features.edx & (1u << 26)))
        kernel_panic("CPU: SSE2 is required for LuaJIT.");

    uint32_t cr0;
    uint32_t cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1u << 2) | (1u << 3));
    cr0 |= (1u << 1) | (1u << 5);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9) | (1u << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("fninit");
}

extern "C" int kernel_main() {
    fb_init(multiboot_addr);
    init_printg();
    show_gbanner(cursor_x, cursor_y);

    int tsw = u16int(sw);
    int tsh = u16int(sh);
    sb_init(tsw, tsh);

    printg(" ");
    printg("[QMS-KERNEL][%s][%s]", CORE_VER, BUILD_TAG);
    printg("[FERRITE-CORE][%s][%s]", FERR_VER, FERR_BUILD_TAG);
    printg(" ");

    multiboot_info* mbi = (multiboot_info*)multiboot_addr;
    if (!mbi) kernel_panic("multiboot_info pointer is null!");

    log(3, "VFS: Initialized...");
    init_gdt();
    init_fpu_sse();
    log(3, "VFS: Init done");

    size_t total_ram_kb = 0;
    if (multiboot_magic == 0x2BADB002 && (mbi->flags & 1)) {
        total_ram_kb = mbi->mem_lower + mbi->mem_upper + 1024;
        log(3, "MM: Detected RAM: %d MB", (int)(total_ram_kb / 1024));
    } else {
        log(2, "MM: Multiboot memory map unavailable. Fallback: 8MB.");
        total_ram_kb = 8192;
    }

    if (total_ram_kb < 4096)
        kernel_panic("MM: Not enough RAM (minimum 4MB required).");

    uintptr_t kernel_start   = 0x100000;
    uintptr_t kernel_end     = (uintptr_t)&end;
    uintptr_t free_mem_start = (kernel_end + 4095) & ~4095UL;
    size_t    usable_bytes   = (total_ram_kb * 1024) - (free_mem_start - kernel_start);

    if (usable_bytes < 1024 * 1024)
        kernel_panic("MM: Usable memory after kernel is less than 1MB.");

    static constexpr size_t KERNEL_MIN       = 2 * 1024 * 1024;
    static constexpr size_t KERNEL_MAX_RATIO = 16;
    static constexpr size_t LUA_QUOTA_SIZE   = 10 * 1024 * 1024;

    size_t kernel_quota = usable_bytes / KERNEL_MAX_RATIO;
    if (kernel_quota < KERNEL_MIN) kernel_quota = KERNEL_MIN;
    kernel_quota = (kernel_quota + 4095) & ~4095UL;

    if (LUA_QUOTA_SIZE+(1*1024*1024)+kernel_quota >= usable_bytes)
        kernel_panic("MM: Not enough RAM to split heaps.");

    size_t lua_quota = LUA_QUOTA_SIZE;
    size_t user_quota = usable_bytes - kernel_quota - lua_quota;

    kernel_mem_manager = new (km_obj) MemoryManager(free_mem_start, kernel_quota);
    lua_mem_manager    = new (lm_obj) MemoryManager(free_mem_start + kernel_quota, lua_quota);
    user_mem_manager   = new (um_obj) MemoryManager(free_mem_start + kernel_quota + lua_quota, user_quota);

    log(3, "IDT: Initializing...");
    init_idt();

    log(3, "PCI: Initializing...");
    pci_init();
    log(3, "PCI: Init done.");

    keyboard_init();

    init_serial();
    serial_println("FERR-QMS Serial initialized");
    serial_println("Ready");

    char vendor[13];
    char model[49];
    get_cpu_vendor(vendor);
    get_cpu_model(model);
    log(3, "CPU: %s %s", vendor, model);


    log(3, "ATA: Scanning ATA drives...");
    int ata_count = ata_init();

    if (ata_count > 0) {
        const AtaDrive* d0 = ata_get_drive(0);
        log(3, "ATA: Primary disk: %s (%u MB)", d0->model, (unsigned)(d0->lba_count / 2048));

        ata_disk0 = new (ata_disk0_buf) AtaDiskIO(0);
    }


    extern void vfs_force_init(void);
    vfs_force_init();

    log(3, "VFS: Initializing FAT16...");
    vfs_fat16_register();

    log(3, "VFS: Initializing FAT32...");
    vfs_fat32_register();

    log(3, "VFS: Initializing QFS...");
    bool qfs_ok = false;
    VFS_Error qfs_reg = vfs_qfs_register();

    if (qfs_reg == VFS_OK || qfs_reg == VFS_ERR_EXISTS) {

        static uint8_t ram_buf[512 * 1024];
        log(3, "VFS: Mounting QFS at / (%u KB)...", (unsigned)(sizeof(ram_buf) / 1024));
        VFS_Error verr = vfs_init("qfs", ram_buf, sizeof(ram_buf), nullptr, 0);

        if (verr == VFS_OK) {
            log(3, "VFS: QFS mounted at /");
            ramfs_primary_addr = ram_buf;
            ramfs_primary_size = sizeof(ram_buf);
            qfs_ok = true;
        } else {
            log(1, "VFS: QFS mount failed: %s (%d)", vfs_strerror(verr), (int)verr);
        }

    } else {
        log(1, "VFS: QFS driver registration failed: %s (%d) — running without VFS",
            vfs_strerror(qfs_reg), (int)qfs_reg);
    }

    if (!qfs_ok) {
        log(2, "VFS: no filesystem available.\nVFS: File operations will not work.");
    }

    log(3, "SB: Scrollbuf: %d lines x %d cols (%d KB)", SB_LINES, SB_COLS, (int)(SB_MEM_SIZE / 1024));

    log(3, "INIT: Kernel initialization completed.");
    if (ata_count >= 1){
        log(3, "Drive Ready");
        log(3, "Run Ferrite Core files setup");
        ferrc_setup();
    }else{
        log(2, "Skip 0 mount. (No ata0 drive)");
    }
    log(3, "INIT: Starting Lua runtime...");
    log(3, "Run lua code");
    lua_run_luac("print('lua is now working in the kernel!')"); // LUA COD !!!
    lua_run_luac("print('кстати русский язык тоже работает!')"); // LUA COD !!!
    lua_run_luac("print('finally')");
    log(3, "Lua test completed.");
    log(3, "INIT: Starting shell...");

    run_shell();

    if (shrboot) {
        reboot();
    } else if (shsdown) {
        shutdown();
    } else {
        kernel_panic("INIT: Shell exited unexpectedly.");
    }
}

extern "C" void out_handler(){
    printg("");
    printg("'''''''''''''''''''''''''''''''''''''''");
    printg("'''''''''''''''''''''''''''''''''''''''");
    printg("OUT OF EXECUTION LIMIT @ HANDLED");
    printg("KERNEL FATAL ERROR");
    printg("KERNEL OUT OF EXECUTION LIMIT");
    printg("NO EXECUTION CONTINUE");
    printg("PLEASE RESTART YOUR PC/VM");
    printg("SYSTEM HALTED");
    printg("'''''''''''''''''''''''''''''''''''''''");
    printg("'''''''''''''''''''''''''''''''''''''''");
    printg("");
    printg("PLEASE RESTART YOUR PC/VM");
    while (1) { __asm__ __volatile__("hlt"); }
}