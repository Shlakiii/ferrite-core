#ifndef _FORMAT_H
#define _FORMAT_H

#include <drivers/hw/ata_diskio.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FS_TYPE_FAT32 = 0,
    FS_TYPE_FAT16 = 1,
    FS_TYPE_QFS   = 2,
} DiskFsType;

int disk_format(IDiskIO* disk_io, uint32_t total_sectors, const char* volume_label, DiskFsType fs_type);
int fat32_format(IDiskIO* disk, uint32_t total_sectors, const char* volume_label);

#ifdef __cplusplus
}
#endif

#endif
