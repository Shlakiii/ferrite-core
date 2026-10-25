#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VFS_MAX_PATH        256
#define VFS_MAX_NAME        64
#define VFS_MAX_FD          32
#define VFS_MAX_MOUNTS      8
#define VFS_MAX_FS_DRIVERS  16
#define VFS_MAX_DIR_ENTRIES 128

#define VFS_ENT_NONE    0
#define VFS_ENT_FILE    1
#define VFS_ENT_DIR     2

#define VFS_ATTR_SYSTEM 0x01
#define VFS_ATTR_HIDDEN 0x02

#define VFS_O_RDONLY    0x00
#define VFS_O_WRONLY    0x01
#define VFS_O_RDWR      0x02
#define VFS_O_CREAT     0x04
#define VFS_O_TRUNC     0x08
#define VFS_O_APPEND    0x10

typedef enum {
    VFS_OK            =  0,
    VFS_ERR_NOT_FOUND = -1,
    VFS_ERR_EXISTS    = -2,
    VFS_ERR_NO_SPACE  = -3,
    VFS_ERR_NOT_FILE  = -4,
    VFS_ERR_NOT_DIR   = -5,
    VFS_ERR_NO_FS     = -6,
    VFS_ERR_BAD_ARG   = -7,
    VFS_ERR_IO        = -8,
    VFS_ERR_TOO_BIG   = -9,
    VFS_ERR_NO_FD     = -10,
    VFS_ERR_BAD_FD    = -11,
    VFS_ERR_NOT_IMPL  = -12,
    VFS_ERR_NOT_EMPTY = -13,
    VFS_ERR_BUSY      = -14,
    VFS_ERR_PERM      = -15,
} VFS_Error;

#ifdef __cplusplus

class IDiskIO {
public:
    virtual bool read_sectors (uint32_t lba, void* buf, uint16_t count) = 0;
    virtual bool write_sectors(uint32_t lba, const void* buf, uint16_t count) = 0;
    virtual ~IDiskIO() {}
};

#endif

typedef struct VFS_Stat {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint8_t  type;
    uint8_t  attributes;
} VFS_Stat;

typedef struct VFS_DirEntry {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint8_t  type;
    uint8_t  attributes;
} VFS_DirEntry;

typedef struct VFS_Dir {
    VFS_DirEntry entries[VFS_MAX_DIR_ENTRIES];
    int          count;
    VFS_Error    error;
} VFS_Dir;

struct VFS_FSDriver;

struct VFS_FSInstance {
    const struct VFS_FSDriver* driver;
    void* priv;
};

struct VFS_FSDriver {
    const char* name;

    VFS_Error (*fs_mount)(struct VFS_FSInstance* inst,
                          void*     addr,
                          size_t    size,
#ifdef __cplusplus
                          IDiskIO*  disk,
#else
                          void*     disk,
#endif
                          uint32_t  lba_start);

    VFS_Error (*fs_unmount)(struct VFS_FSInstance* inst);
    VFS_Dir   (*fs_readdir)(struct VFS_FSInstance* inst, int dir_id);
    int       (*fs_lookup)(struct VFS_FSInstance* inst, const char* name, int parent_id);
    int       (*fs_parent)(struct VFS_FSInstance* inst, int id);
    const char* (*fs_name)(struct VFS_FSInstance* inst, int id);
    uint8_t   (*fs_type)(struct VFS_FSInstance* inst, int id);
    uint32_t  (*fs_size)(struct VFS_FSInstance* inst, int id);
    VFS_Error (*fs_create)(struct VFS_FSInstance* inst, const char* name, uint8_t type, int parent_id);
    VFS_Error (*fs_remove)(struct VFS_FSInstance* inst, int id);
    VFS_Error (*fs_read)(struct VFS_FSInstance* inst, int id, void* buf, uint32_t* inout_size);
    VFS_Error (*fs_write)(struct VFS_FSInstance* inst, int id, const void* buf, uint32_t size);
    uint8_t   (*fs_get_attributes)(struct VFS_FSInstance* inst, int id);
    VFS_Error (*fs_set_attributes)(struct VFS_FSInstance* inst, int id, uint8_t attributes);
};

#ifdef __cplusplus
extern "C" {
#endif

VFS_Error vfs_register_driver(const struct VFS_FSDriver* drv);

const struct VFS_FSDriver* vfs_find_driver(const char* name);
int vfs_list_drivers(const struct VFS_FSDriver** out, int max);

VFS_Error vfs_init(const char* fs_name, void* addr, size_t size,
#ifdef __cplusplus
                   IDiskIO* disk,
#else
                   void*    disk,
#endif
                   uint32_t lba_start);

VFS_Error vfs_mount(const char* mount_point, const char* fs_name, void* addr, size_t size,
#ifdef __cplusplus
                    IDiskIO* disk,
#else
                    void*    disk,
#endif
                    uint32_t lba_start,
                    const char* source_path);

VFS_Error vfs_umount(const char* mount_point);

int       vfs_open (const char* path, int flags);
VFS_Error vfs_close(int fd);
int       vfs_read (int fd, void* buf, uint32_t size);
int       vfs_write(int fd, const void* buf, uint32_t size);
int       vfs_seek (int fd, int32_t offset, int whence);
VFS_Error vfs_stat (int fd, struct VFS_Stat* out);

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

VFS_Error vfs_creat  (const char* path, uint8_t type);
VFS_Error vfs_setattribute(const char* path, const char* attributes);
VFS_Error vfs_unlink (const char* path);
VFS_Error vfs_mkdir  (const char* path);
VFS_Error vfs_rmdir  (const char* path);
VFS_Error vfs_rename (const char* old_path, const char* new_path);

VFS_Error vfs_read_file (const char* path, void* buf, uint32_t* inout_size);
VFS_Error vfs_write_file(const char* path, const void* buf, uint32_t size);

VFS_Error vfs_stat_path(const char* path, struct VFS_Stat* out);

VFS_Dir   vfs_readdir(const char* path);
bool      vfs_cd     (const char* path);
void      vfs_pwd    ();
const char* vfs_getcwd();

const char* vfs_strerror(VFS_Error err);

void vfs_force_init(void);
void vfs_dump_mounts();
void vfs_set_current_user(bool is_root);

#ifdef __cplusplus
}
#endif

#endif
