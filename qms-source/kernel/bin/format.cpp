#include "format.h"

#include <lib/strtools/string.h>

#include <io/printg.h>

#include <drivers/hw/ata_diskio.h>

#include <stdint.h>

struct Fat32BootSector {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t signature;
} __attribute__((packed));

struct Fat16BootSector {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[448];
    uint16_t signature;
} __attribute__((packed));

static void write_zeros(IDiskIO* disk, uint32_t lba, uint32_t count) {
    uint8_t zero[512] = {0};
    for (uint32_t i = 0; i < count; i++)
        disk->write_sectors(lba + i, zero, 1);
}

static int do_fat32_format(IDiskIO* disk, uint32_t total_sectors, const char* label) {
    if (total_sectors < 65536) {
        printgc(12, 0, "format: too small for FAT32 (need >= 32 MB)");
        return -1;
    }

    Fat32BootSector bs;
    memset(&bs, 0, sizeof(bs));

    bs.jmp[0] = 0xEB; bs.jmp[1] = 0x58; bs.jmp[2] = 0x90;
    memcpy(bs.oem, "QMSFAT32", 8);
    bs.bytes_per_sector    = 512;
    bs.sectors_per_cluster = 8;
    bs.reserved_sectors    = 32;
    bs.fat_count           = 2;
    bs.media_type          = 0xF8;
    bs.sectors_per_track   = 63;
    bs.heads               = 255;
    bs.total_sectors_32    = total_sectors;

    uint32_t clusters    = (total_sectors - bs.reserved_sectors) / bs.sectors_per_cluster;
    uint32_t fat_sectors = ((clusters * 4) + 511) / 512;

    bs.fat_size_32        = fat_sectors;
    bs.root_cluster       = 2;
    bs.fs_info_sector     = 1;
    bs.backup_boot_sector = 6;
    bs.drive_number       = 0x80;
    bs.boot_signature     = 0x29;
    bs.volume_id          = 0xDEADBEEF;

    char padded[11];
    memset(padded, ' ', 11);
    if (label && label[0]) {
        int l = strlen(label);
        if (l > 11) l = 11;
        memcpy(padded, label, l);
    } else {
        memcpy(padded, "QMS_VOLUME ", 11);
    }
    memcpy(bs.volume_label, padded, 11);
    memcpy(bs.fs_type, "FAT32   ", 8);
    bs.signature = 0xAA55;

    if (!disk->write_sectors(0, &bs, 1)) {
        printgc(12, 0, "format: boot sector write failed");
        return -2;
    }
    disk->write_sectors(6, &bs, 1);

    uint8_t fsinfo[512] = {0};
    *(uint32_t*)(fsinfo + 0)   = 0x41615252;
    *(uint32_t*)(fsinfo + 484) = 0x61417272;
    *(uint32_t*)(fsinfo + 488) = 0xFFFFFFFF;
    *(uint32_t*)(fsinfo + 492) = 0xFFFFFFFF;
    *(uint32_t*)(fsinfo + 508) = 0xAA550000;
    disk->write_sectors(1, fsinfo, 1);

    uint32_t fat1_lba = bs.reserved_sectors;
    uint32_t fat2_lba = fat1_lba + fat_sectors;
    write_zeros(disk, fat1_lba, fat_sectors);
    write_zeros(disk, fat2_lba, fat_sectors);

    uint8_t fat0[512] = {0};
    uint32_t* ft = (uint32_t*)fat0;
    ft[0] = 0x0FFFFFF8;
    ft[1] = 0x0FFFFFFF;
    ft[2] = 0x0FFFFFFF;
    disk->write_sectors(fat1_lba, fat0, 1);
    disk->write_sectors(fat2_lba, fat0, 1);

    uint32_t root_lba = fat2_lba + fat_sectors;
    write_zeros(disk, root_lba, bs.sectors_per_cluster);

    printgc(10, 0, "FAT32 format done. Label: %s | Cluster: %u KiB",
            bs.volume_label, (bs.sectors_per_cluster * 512) / 1024);
    return 0;
}

static int do_fat16_format(IDiskIO* disk, uint32_t total_sectors, const char* label) {
    if (total_sectors < 4096 || total_sectors > 131072) {
        printgc(12, 0, "format: FAT16 suitable for 2-64 MB volumes");
        return -1;
    }

    Fat16BootSector bs;
    memset(&bs, 0, sizeof(bs));

    bs.jmp[0] = 0xEB; bs.jmp[1] = 0x3C; bs.jmp[2] = 0x90;
    memcpy(bs.oem, "QMSFAT16", 8);
    bs.bytes_per_sector    = 512;
    bs.sectors_per_cluster = (total_sectors > 32768) ? 4 : 2;
    bs.reserved_sectors    = 4;
    bs.fat_count           = 2;
    bs.root_entries        = 512;
    bs.media_type          = 0xF8;
    bs.sectors_per_track   = 63;
    bs.heads               = 255;

    if (total_sectors <= 65535)
        bs.total_sectors_16 = (uint16_t)total_sectors;
    else
        bs.total_sectors_32 = total_sectors;

    uint32_t root_sectors = (bs.root_entries * 32 + 511) / 512;
    uint32_t data_sectors = total_sectors - bs.reserved_sectors - root_sectors;
    uint32_t clusters     = data_sectors / bs.sectors_per_cluster;
    uint32_t fat_sectors  = ((clusters * 2) + 511) / 512;

    bs.fat_size_16    = (uint16_t)fat_sectors;
    bs.drive_number   = 0x80;
    bs.boot_signature = 0x29;
    bs.volume_id      = 0xBEEFCAFE;

    char padded[11];
    memset(padded, ' ', 11);
    if (label && label[0]) {
        int l = strlen(label);
        if (l > 11) l = 11;
        memcpy(padded, label, l);
    } else {
        memcpy(padded, "QMS_FAT16  ", 11);
    }
    memcpy(bs.volume_label, padded, 11);
    memcpy(bs.fs_type, "FAT16   ", 8);
    bs.signature = 0xAA55;

    if (!disk->write_sectors(0, &bs, 1)) {
        printgc(12, 0, "format: FAT16 boot sector write failed");
        return -2;
    }

    write_zeros(disk, bs.reserved_sectors, fat_sectors * 2 + root_sectors);

    uint8_t fat0[512] = {0};
    ((uint16_t*)fat0)[0] = 0xFFF8;
    ((uint16_t*)fat0)[1] = 0xFFFF;
    disk->write_sectors(bs.reserved_sectors, fat0, 1);
    disk->write_sectors(bs.reserved_sectors + fat_sectors, fat0, 1);

    printgc(10, 0, "FAT16 format done. Label: %s | Cluster: %u KiB", bs.volume_label, (bs.sectors_per_cluster * 512) / 1024);
    return 0;
}

extern "C" int disk_format(IDiskIO* disk_io, uint32_t total_sectors, const char* volume_label, DiskFsType fs_type) {
    if (!disk_io || total_sectors == 0) {
        printgc(12, 0, "format: invalid parameters");
        return -1;
    }

    switch (fs_type) {
        case FS_TYPE_FAT32:
            printgc(7, 0, "Formatting %u sectors as FAT32...", total_sectors);
            return do_fat32_format(disk_io, total_sectors, volume_label);

        case FS_TYPE_FAT16:
            printgc(7, 0, "Formatting %u sectors as FAT16...", total_sectors);
            return do_fat16_format(disk_io, total_sectors, volume_label);

        default:
            printgc(12, 0, "format: unknown filesystem type");
            return -3;
    }
}

extern "C" int fat32_format(IDiskIO* disk_io, uint32_t total_sectors, const char* volume_label) {
    return disk_format(disk_io, total_sectors, volume_label, FS_TYPE_FAT32);
}
