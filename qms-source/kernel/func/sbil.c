#include "sbil.h"
#include "sbik.h"
#include "vfs.h"

#include <lib/metad.h>
#include <lib/rmem/memory.h>
#include <lib/elf/elf.h>
#include <lib/math/math.h>
#include <lib/strtools/string.h>
#include <lib/strtools/ctype.h>
#include <lib/cpu/power.h>
#include <lib/cpu/cpuid.h>
#include <lib/font/fload.h>

#include <io/keyboard.h>
#include <io/printg.h>
#include <io/serial.h>
#include <io/fb.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef PT_LOAD
#define PT_LOAD 1
#endif
#ifndef SELFMAG
#define SELFMAG 4
#endif
#ifndef SHT_REL
#define SHT_REL 9
#endif
#ifndef R_386_32
#define R_386_32 1
#endif

typedef struct {
    uint32_t sh_name, sh_type, sh_flags, sh_addr;
    uint32_t sh_offset, sh_size, sh_link, sh_info;
    uint32_t sh_addralign, sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

#define ELF32_R_TYPE(i) ((i) & 0xff)

extern MemoryManager* kernel_mem_manager;
static inline void* kmalloc(size_t sz) { return kernel_mem_manager->allocate(sz); }
static inline void  kfree(void* p)     { kernel_mem_manager->free(p); }

static const uint16_t k_supported[] = SBI_SUPPORTED_SDK_VERSIONS;

int sbil_sdk_supported(uint16_t v) {
    for (int i = 0; i < SBI_SUPPORTED_SDK_COUNT; i++)
        if (k_supported[i] == v) return 1;
    return 0;
}

const char* sbil_strerror(SbilError e) {
    switch (e) {
        case SBIL_OK:               return "OK";
        case SBIL_ERR_BAD_MAGIC:    return "Not a .qex file (bad magic)";
        case SBIL_ERR_SDK_INCOMPAT: return "Incompatible SDK version";
        case SBIL_ERR_NO_MEMORY:    return "Out of memory";
        case SBIL_ERR_BAD_PATH:     return "Invalid path";
        case SBIL_ERR_FS_READ:      return "Filesystem read error";
        case SBIL_ERR_ROOT_DENIED:  return "App requires root privileges";
        case SBIL_ERR_FILE_TOOBIG:  return "File too large";
        case SBIL_ERR_BAD_ENTRY:    return "entry_point out of bounds";
        default:                    return "Unknown error";
    }
}

static void apply_relocations(uint8_t* file_buf, uint8_t* load_buf, uint32_t min_vaddr, uint32_t mem_size, uint32_t file_size) {
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)file_buf;
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) return;

    Elf32_Shdr* shdr = (Elf32_Shdr*)(file_buf + ehdr->e_shoff);
    uint32_t reloc_delta = (uint32_t)load_buf - min_vaddr;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_REL) continue;
        if (shdr[i].sh_offset + shdr[i].sh_size > file_size) continue;

        Elf32_Rel* rels = (Elf32_Rel*)(file_buf + shdr[i].sh_offset);
        uint32_t count = shdr[i].sh_size / sizeof(Elf32_Rel);

        uint32_t target_sh = shdr[i].sh_info;
        if (target_sh >= (uint32_t)ehdr->e_shnum) continue;

        uint32_t target_vaddr = shdr[target_sh].sh_addr;

        for (uint32_t j = 0; j < count; j++) {
            if (ELF32_R_TYPE(rels[j].r_info) != R_386_32) continue;

            uint32_t patch_vaddr = target_vaddr + rels[j].r_offset;
            if (patch_vaddr < min_vaddr) continue;
            uint32_t patch_off = patch_vaddr - min_vaddr;
            if (patch_off + 4 > mem_size) continue;

            uint32_t* slot = (uint32_t*)(load_buf + patch_off);
            *slot += reloc_delta;
        }
    }
}

static SbilError sbil_exec_buf(void* file_buf, uint32_t size, int system_root, SbilAppDesc* out_desc) {
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)file_buf;

    if (memcmp(ehdr->e_ident, "\x7f\x45\x4c\x46", 4) != 0) {
        printg("[SBIL] Error: Not a valid ELF file\n");
        return SBIL_ERR_BAD_MAGIC;
    }

    uint32_t min_vaddr = 0xFFFFFFFFu, max_vaddr = 0;
    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint8_t*)file_buf + ehdr->e_phoff);
    int has_loadable = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
            if (end > max_vaddr) max_vaddr = end;
            has_loadable = 1;
        }
    }

    if (!has_loadable || max_vaddr <= min_vaddr) {
        printg("[SBIL] Error: No valid PT_LOAD segments\n");
        return SBIL_ERR_BAD_ENTRY;
    }

    uint32_t mem_needed = max_vaddr - min_vaddr;
    void* buffer = kmalloc(mem_needed);
    if (!buffer) return SBIL_ERR_NO_MEMORY;
    memset(buffer, 0, mem_needed);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_filesz > 0) {
            memcpy((uint8_t*)buffer + (phdr[i].p_vaddr - min_vaddr), (uint8_t*)file_buf + phdr[i].p_offset, phdr[i].p_filesz);
        }
    }

    apply_relocations((uint8_t*)file_buf, (uint8_t*)buffer, min_vaddr, mem_needed, size);

    sbi_header_t* hdr = NULL;

    if (ehdr->e_shoff && ehdr->e_shnum && ehdr->e_shstrndx < ehdr->e_shnum) {
        Elf32_Shdr* shdr = (Elf32_Shdr*)((uint8_t*)file_buf + ehdr->e_shoff);
        uint32_t shstr_off = shdr[ehdr->e_shstrndx].sh_offset;
        if (shstr_off < size) {
            char* shstrtab = (char*)((uint8_t*)file_buf + shstr_off);
            for (int i = 0; i < ehdr->e_shnum && !hdr; i++) {
                if (shdr[i].sh_offset + shdr[i].sh_size > size) continue;
                if (strcmp(shstrtab + shdr[i].sh_name, ".text.head") == 0) {
                    uint32_t va = shdr[i].sh_addr;
                    if (va >= min_vaddr &&
                        va - min_vaddr + sizeof(sbi_header_t) <= mem_needed)
                        hdr = (sbi_header_t*)((uint8_t*)buffer + (va - min_vaddr));
                }
            }
        }
    }

    if (!hdr) {
        for (uint32_t off = 0; off + sizeof(sbi_header_t) <= mem_needed; off += 4) {
            sbi_header_t* c = (sbi_header_t*)((uint8_t*)buffer + off);
            if (c->magic == SBI_MAGIC) { hdr = c; break; }
        }
    }

    if (!hdr) {
        printg("[SBIL] Error: SBI header not found\n");
        kfree(buffer);
        return SBIL_ERR_BAD_MAGIC;
    }

    if (!sbil_sdk_supported(hdr->sdk_ver)) {
        printg("[SBIL] Error: Unsupported SDK version %d\n", (int)hdr->sdk_ver);
        kfree(buffer);
        return SBIL_ERR_SDK_INCOMPAT;
    }

    if (hdr->is_root && !system_root) {
        printg("[SBIL] Error: Root privileges required\n");
        kfree(buffer);
        return SBIL_ERR_ROOT_DENIED;
    }

    int grant_root = (hdr->is_root && system_root);

    uint32_t nr_phys = hdr->noroot_tab_off;
    uint32_t r_phys  = hdr->root_tab_off;
    uint32_t buf_base = (uint32_t)buffer;
    uint32_t buf_end  = buf_base + mem_needed;

    if (nr_phys < buf_base || nr_phys + sizeof(sbi_noroot_t) > buf_end || r_phys  < buf_base || r_phys  + sizeof(sbi_root_t)   > buf_end)
    {
        printg("[SBIL] Error: SBI table pointers out of buffer\n");
        kfree(buffer);
        return SBIL_ERR_BAD_ENTRY;
    }

    sbi_noroot_t* app_nr = (sbi_noroot_t*)nr_phys;
    sbi_root_t*   app_r  = (sbi_root_t*)r_phys;

    #define FILL_NOROOT(id, ret, name, params, kernel_func) \
        if (id < SBI_NOROOT_MAX) \
            app_nr->name = (ret (*) params)kernel_func;

    SBI_NOROOT_ENTRIES(FILL_NOROOT)

    #define FILL_ROOT(id, ret, name, params, kernel_func) \
        if (id < SBI_ROOT_MAX) \
            app_r->name = grant_root ? (ret (*) params)kernel_func : NULL;

    SBI_ROOT_ENTRIES(FILL_ROOT)

    if (out_desc) {
        strncpy(out_desc->app_name, hdr->app_name, 16);
        out_desc->app_name[15] = '\0';
        out_desc->sdk_ver   = hdr->sdk_ver;
        out_desc->is_root   = (uint16_t)grant_root;
        out_desc->load_base = buffer;
        out_desc->load_size = mem_needed;
        out_desc->exit_code = 0;
    }

    if (ehdr->e_entry < min_vaddr || ehdr->e_entry - min_vaddr >= mem_needed) {
        printg("[SBIL] Error: e_entry 0x%x out of bounds\n", ehdr->e_entry);
        kfree(buffer);
        return SBIL_ERR_BAD_ENTRY;
    }

    uint32_t entry_offset = ehdr->e_entry - min_vaddr;
    void (*entry_func)(void) = (void (*)(void))((uint8_t*)buffer + entry_offset);

    entry_func();
    return SBIL_OK;
}

SbilError sbil_exec(const char* path, int system_root, SbilAppDesc* out_desc) {
    if (!path) return SBIL_ERR_BAD_PATH;

    while (*path == ' ') path++;
    if (!*path) return SBIL_ERR_BAD_PATH;

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        printg("[SBIL] File not found: %s\n", path);
        return SBIL_ERR_BAD_PATH;
    }

    struct VFS_Stat st;
    if (vfs_stat(fd, &st) != VFS_OK) {
        vfs_close(fd);
        return SBIL_ERR_FS_READ;
    }

    if (st.size == 0) {
        printg("[SBIL] Error: File is empty\n");
        vfs_close(fd);
        return SBIL_ERR_FS_READ;
    }

    if (st.size > 1024u * 1024u * 16u) {
        vfs_close(fd);
        return SBIL_ERR_FILE_TOOBIG;
    }

    uint32_t magic = 0;
    vfs_read(fd, &magic, sizeof(uint32_t));
    if (memcmp(&magic, "\x7f\x45\x4c\x46", 4) != 0) {
        printg("[SBIL] Error: Invalid file format\n");
        vfs_close(fd);
        return SBIL_ERR_BAD_MAGIC;
    }
    vfs_seek(fd, 0, VFS_SEEK_SET);

    uint8_t* buf = (uint8_t*)kmalloc(st.size);
    if (!buf) {
        vfs_close(fd);
        return SBIL_ERR_NO_MEMORY;
    }

    if (vfs_read(fd, buf, st.size) != (int)st.size) {
        kfree(buf);
        vfs_close(fd);
        return SBIL_ERR_FS_READ;
    }
    vfs_close(fd);

    SbilError result = sbil_exec_buf(buf, st.size, system_root, out_desc);
    kfree(buf);
    return result;
}

SbilError sbil_check(const char* path) {
    if (!path) return SBIL_ERR_BAD_PATH;
    while (*path == ' ') path++;
    if (!*path) return SBIL_ERR_BAD_PATH;

    struct VFS_Stat st;
    if (vfs_stat_path(path, &st) != VFS_OK)
        return SBIL_ERR_FS_READ;

    if (st.size < sizeof(Elf32_Ehdr))
        return SBIL_ERR_BAD_MAGIC;

    uint8_t ident[4];
    uint32_t sz = 4;
    if (vfs_read_file(path, ident, &sz) != VFS_OK || sz < 4)
        return SBIL_ERR_FS_READ;

    if (memcmp(ident, "\x7f\x45\x4c\x46", 4) != 0)
        return SBIL_ERR_BAD_MAGIC;

    uint8_t* fbuf = (uint8_t*)kmalloc(st.size);
    if (!fbuf) return SBIL_OK;

    uint32_t fsz = st.size;
    if (vfs_read_file(path, fbuf, &fsz) == VFS_OK) {
        int found = 0;
        for (uint32_t off = 0; off + 4 <= fsz; off += 4) {
            uint32_t v;
            memcpy(&v, fbuf + off, 4);
            if (v == SBI_MAGIC) { found = 1; break; }
        }
        kfree(fbuf);
        return found ? SBIL_OK : SBIL_ERR_BAD_MAGIC;
    }
    kfree(fbuf);
    return SBIL_OK;
}

const char* sbil_get_arg() {
    return current_app_arg[0] ? current_app_arg : "";
}

void sbil_clear_arg() {
    current_app_arg[0] = '\0';
}

void sbil_set_arg(const char* arg) {
    if (arg && *arg) {
        strncpy(current_app_arg, arg, sizeof(current_app_arg)-1);
        current_app_arg[sizeof(current_app_arg)-1] = '\0';
    } else {
        current_app_arg[0] = '\0';
    }
}

SbilError sbil_run(const char* path) {
    return sbil_exec(path, 0, NULL);
}
