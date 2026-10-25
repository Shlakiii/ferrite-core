#ifndef SBI_H
#define SBI_H

#include <stdint.h>
#include <stddef.h>

#include <io/io.h>

#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

void* user_malloc(size_t size);
void  user_free(void* p);
void* k_allocate(size_t size);
void  k_free(void* p);
void* k_realloc(void* ptr, size_t new_size);

#ifdef __cplusplus
}
#endif

typedef enum {
    SBIL_OK               =  0,
    SBIL_ERR_BAD_MAGIC    = -1,
    SBIL_ERR_SDK_INCOMPAT = -2,
    SBIL_ERR_NO_MEMORY    = -3,
    SBIL_ERR_BAD_PATH     = -4,
    SBIL_ERR_NO_SLOT      = -5,
    SBIL_ERR_FS_READ      = -6,
    SBIL_ERR_ROOT_DENIED  = -7,
    SBIL_ERR_FILE_TOOBIG  = -8,
    SBIL_ERR_BAD_ENTRY    = -9,
} SbilError;

typedef struct {
    char     app_name[16];
    uint16_t sdk_ver;
    uint16_t is_root;
    void*    load_base;
    uint32_t load_size;
    int      exit_code;
} SbilAppDesc;

#define SBI_MAGIC       0x514245
#define SBI_SDK_VER     4

#define SBI_SUPPORTED_SDK_VERSIONS { 3, 4 }
#define SBI_SUPPORTED_SDK_COUNT    2

#define SBI_NOROOT_MAX   1024
#define SBI_ROOT_MAX     512

#define SBI_NOROOT_ENTRIES(X) \
    X(1,   const char*, get_arg,   (void), sbil_get_arg) \
    X(2,   void,        clear_arg, (void), sbil_clear_arg) \
    X(3,   void,        set_arg,   (const char* arg), sbil_set_arg) \
    X(4,   void,        log,       (int level, const char* fmt, ...), log) \
    X(5,   void,        printg,    (const char* fmt, ...), printg) \
    X(6,   void,        printgc,   (int t, int b, const char* fmt, ...), printgc) \
    X(7,   void,        clear_screen, (void), clear_screen) \
    X(8,   void,        goto_xy,   (int x, int y), goto_xy) \
    X(9,   int,         tscreen_h, (void), tscreen_h) \
    X(10,  int,         tscreen_w, (void), tscreen_w) \
    X(11,  int,         tcursor_x, (void), tcursor_x) \
    X(12,  int,         tcursor_y, (void), tcursor_y) \
    X(13,  void,        serial_print, (const char* str), serial_print) \
    X(14,  char,        read_serial, (void), read_serial) \
    X(15,  int,         serial_received, (void), serial_received) \
    X(16,  char,        get_char,  (void), get_char) \
    X(17,  uint8_t,     kbd_get_modifiers, (void), kbd_get_modifiers) \
    X(18,  uint8_t,     get_special_key, (void), get_special_key) \
    X(19,  char*,       math_add,  (const char* a, const char* b), add) \
    X(20,  char*,       math_sub,  (const char* a, const char* b), sub) \
    X(21,  char*,       math_mul,  (const char* a, const char* b), mul) \
    X(22,  char*,       math_div,  (const char* a, const char* b), div) \
    X(23,  int,         math_compare, (const char* a, const char* b), compare) \
    X(24,  void*,       memset,    (void* ptr, int value, size_t num), memset) \
    X(25,  void*,       memcpy,    (void* destination, const void* source, size_t num), memcpy) \
    X(26,  int,         memcmp,    (const void* ptr1, const void* ptr2, size_t num), memcmp) \
    X(27,  int,         strcmp,    (const char* str1, const char* str2), strcmp) \
    X(28,  int,         strncmp,   (const char* str1, const char* str2, size_t num), strncmp) \
    X(29,  size_t,      strlen,    (const char* str), strlen) \
    X(30,  char*,       strcpy,    (char* destination, const char* source), strcpy) \
    X(31,  char*,       strncpy,   (char* destination, const char* source, size_t num), strncpy) \
    X(32,  char*,       strcat,    (char* dest, const char* src), strcat) \
    X(33,  char*,       strchr,    (const char* str, int character), strchr) \
    X(34,  char*,       strstr,    (const char* haystack, const char* needle), strstr) \
    X(35,  char*,       strtok,    (char* str, const char* delimiters), strtok) \
    X(36,  int,         u16int,    (uint16_t value), u16int) \
    X(37,  uint16_t,    intu16,    (int value), intu16) \
    X(38,  void*,       malloc,    (size_t size), user_malloc) \
    X(39,  void,        free,      (void* p), user_free) \
    X(40,  void,        get_cpu_vendor, (char* vendor), get_cpu_vendor) \
    X(41,  void,        get_cpu_model,  (char* model), get_cpu_model) \
    X(42,  const char*, get_core_name, (void), get_core_name) \
    X(43,  const char*, get_core_ver,  (void), get_core_ver) \
    X(44,  const char*, get_core_tag,  (void), get_core_tag) \
    X(45,  const char*, get_core_arch, (void), get_core_arch) \
    X(46,  int,         fload_set, (const char* name), fload_set) \
    X(47,  uint32_t,    fload_get_count, (void), fload_get_count) \
    X(48,  const char*, fload_get_name_index, (uint32_t index), fload_get_name_index) \
    X(49,  int,         vfs_open,  (const char* path, int flags), vfs_open) \
    X(50,  VFS_Error,   vfs_close, (int fd), vfs_close) \
    X(51,  int,         vfs_read,  (int fd, void* buf, uint32_t size), vfs_read) \
    X(52,  int,         vfs_write, (int fd, const void* buf, uint32_t size), vfs_write) \
    X(53,  int,         vfs_seek,  (int fd, int32_t offset, int whence), vfs_seek) \
    X(54,  VFS_Error,   vfs_stat_path, (const char* path, struct VFS_Stat* out), vfs_stat_path) \
    X(55,  VFS_Dir,     vfs_readdir, (const char* path), vfs_readdir) \
    X(56,  VFS_Error,   vfs_mkdir, (const char* path), vfs_mkdir) \
    X(57,  VFS_Error,   vfs_rmdir, (const char* path), vfs_rmdir) \
    X(58,  VFS_Error,   vfs_unlink,(const char* path), vfs_unlink) \
    X(59,  const char*, vfs_getcwd, (void), vfs_getcwd) \
    X(60,  bool,        vfs_cd,    (const char* path), vfs_cd) \
    X(61,  void,        show_gbanner, (int x, int y), show_gbanner) \
    X(62,  void,        show_banner, (void), show_banner) \
    X(63,  SbilError,   sbil_run,  (const char* path), sbil_run) \
    X(64,  SbilError,   sbil_check, (const char* path), sbil_check) \
    X(65,  VFS_Error,   vfs_setattribute, (const char* path, const char* attributes), vfs_setattribute)

#define SBI_ROOT_ENTRIES(X) \
    X(1,   uint8_t,     inb, (uint16_t port), inb) \
    X(2,   void,        outb, (uint16_t port, uint8_t value), outb) \
    X(3,   uint16_t,    inw, (uint16_t port), inw) \
    X(4,   void,        outw, (uint16_t port, uint16_t value), outw) \
    X(5,   void*,       k_allocate, (size_t size), k_allocate) \
    X(6,   void,        k_free, (void* p), k_free) \
    X(7,   void*,       k_realloc, (void* ptr, size_t new_size), k_realloc) \
    X(8,   VFS_Error,   vfs_register_driver, (const struct VFS_FSDriver* drv), vfs_register_driver) \
    X(9,   VFS_Error,   vfs_mount, (const char* mp, const char* fs, void* adr, size_t sz, void* dsk, uint32_t lba, const char* src), vfs_mount) \
    X(10,  VFS_Error,   vfs_umount, (const char* mount_point), vfs_umount) \
    X(11,  void,        vfs_force_init, (void), vfs_force_init) \
    X(12,  int,         fload_set_data, (const uint8_t* data), fload_set_data) \
    X(13,  void,        fb_putpixel, (int x, int y, uint32_t color), fb_putpixel) \
    X(14,  void,        fb_clear, (uint32_t color), fb_clear) \
    X(15,  void,        fb_draw_rect, (int x, int y, int w, int h, uint32_t color), fb_draw_rect) \
    X(16,  void,        fb_draw_char, (int x, int y, char c, uint32_t fg, uint32_t bg), fb_draw_char) \
    X(17,  void,        fb_draw_image, (int x, int y, uint32_t w, uint32_t h, uint32_t* data), fb_draw_image) \
    X(18,  void,        reboot, (void), reboot) \
    X(19,  void,        shutdown, (void), shutdown)

#define AS_NOROOT_MEMBER(id, ret, name, params, kernel)   ret (*name) params;
#define AS_ROOT_MEMBER(id, ret, name, params, kernel)     ret (*name) params;

typedef struct {
    SBI_NOROOT_ENTRIES(AS_NOROOT_MEMBER)
    void* reserved[SBI_NOROOT_MAX - 65 ];
} sbi_noroot_t;

typedef struct {
    SBI_ROOT_ENTRIES(AS_ROOT_MEMBER)
    void* reserved[SBI_ROOT_MAX - 19 ];
} sbi_root_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t sdk_ver;
    uint16_t is_root;
    char     app_name[16];
    uint32_t entry_point;
    uint32_t noroot_tab_off;
    uint32_t root_tab_off;
} sbi_header_t;

#endif
