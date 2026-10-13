#ifndef VFS_FAT32_H
#define VFS_FAT32_H

#include <func/vfs.h>
#include "fat32.h"

#ifdef __cplusplus
extern "C" {
#endif

VFS_Error vfs_fat32_register(void);

#ifdef __cplusplus
}
#endif

#endif
