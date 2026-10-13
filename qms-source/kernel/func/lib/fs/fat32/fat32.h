#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/strtools/string.h>

#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LFN          0x0F

#define FAT32_EOC               0x0FFFFFF8u
#define FAT32_FREE              0x00000000u
#define FAT32_BAD               0x0FFFFFF7u

struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed));

struct fat32_dir_entry {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} __attribute__((packed));

struct fat32_fs {
    int      drive;
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t fat_size;
    uint32_t bytes_per_sector;
    uint32_t total_clusters;

    uint8_t  cluster_buffer[4096];
    uint32_t cached_cluster;
    uint32_t fat_cache[128];
    uint32_t fat_cache_sector;
    bool     mounted;
};

struct fat32_file {
    struct fat32_fs *fs;
    uint32_t start_cluster;
    uint32_t current_cluster;
    uint32_t size;
    uint32_t position;
    uint8_t  attr;
    char     name[256];
};

struct fat32_dir {
    struct fat32_fs *fs;
    uint32_t cluster;
    uint32_t entry_index;
};

#ifdef __cplusplus
extern "C" {
#endif

int fat32_mount(struct fat32_fs *fs, int drive, uint32_t partition_lba);
void fat32_unmount(struct fat32_fs *fs);

int fat32_open(struct fat32_fs *fs, const char *path, struct fat32_file *file);
int fat32_read(struct fat32_file *file, void *buffer, size_t size);
int fat32_write(struct fat32_file *file, const void *buffer, size_t size);
int fat32_close(struct fat32_file *file);

int fat32_create(struct fat32_fs *fs, const char *path, uint8_t attr);
int fat32_delete(struct fat32_fs *fs, const char *path);

int fat32_opendir(struct fat32_fs *fs, const char *path, struct fat32_dir *dir);
int fat32_readdir(struct fat32_dir *dir, struct fat32_file *file);
int fat32_mkdir(struct fat32_fs *fs, const char *path);

int fat32_flush_entry(struct fat32_file *file);

#ifdef __cplusplus
}
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
