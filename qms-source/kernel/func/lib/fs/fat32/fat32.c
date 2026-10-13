#include "fat32.h"

#include <lib/strtools/string.h>

#include <drivers/hw/ata_diskio.h>
#include <drivers/hw/ata_pio.h>

#include <io/printg.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int read_sector(struct fat32_fs *fs, uint32_t lba, void *buffer) {
    return ata_read_sectors(fs->drive, fs->partition_lba + lba, 1, buffer);
}

static int write_sector(struct fat32_fs *fs, uint32_t lba, const void *buffer) {
    return ata_write_sectors(fs->drive, fs->partition_lba + lba, 1, buffer);
}

static uint32_t cluster_to_lba(struct fat32_fs *fs, uint32_t cluster) {
    return fs->data_lba + (cluster - 2) * fs->sectors_per_cluster;
}

static int read_cluster(struct fat32_fs *fs, uint32_t cluster, void *buffer) {
    uint32_t lba = cluster_to_lba(fs, cluster);
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (read_sector(fs, lba + i, (uint8_t*)buffer + i * 512) < 0)
            return -1;
    }
    return 0;
}

static int write_cluster(struct fat32_fs *fs, uint32_t cluster, const void *buffer) {
    uint32_t lba = cluster_to_lba(fs, cluster);
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (write_sector(fs, lba + i, (uint8_t*)buffer + i * 512) < 0)
            return -1;
    }
    return 0;
}

static uint32_t fat_read(struct fat32_fs *fs, uint32_t cluster) {
    uint32_t fat_sector = cluster / 128;
    uint32_t fat_offset = cluster % 128;
    
    if (fs->fat_cache_sector != fat_sector) {
        if (read_sector(fs, fs->fat_lba + fat_sector, fs->fat_cache) < 0)
            return FAT32_EOC;
        fs->fat_cache_sector = fat_sector;
    }
    
    return fs->fat_cache[fat_offset] & 0x0FFFFFFF;
}

static int fat_write(struct fat32_fs *fs, uint32_t cluster, uint32_t value) {
    uint32_t fat_sector = cluster / 128;
    uint32_t fat_offset = cluster % 128;
    
    if (fs->fat_cache_sector != fat_sector) {
        if (read_sector(fs, fs->fat_lba + fat_sector, fs->fat_cache) < 0)
            return -1;
        fs->fat_cache_sector = fat_sector;
    }
    
    fs->fat_cache[fat_offset] = (fs->fat_cache[fat_offset] & 0xF0000000) | (value & 0x0FFFFFFF);
    return write_sector(fs, fs->fat_lba + fat_sector, fs->fat_cache);
}

static uint32_t alloc_cluster(struct fat32_fs *fs) {
    for (uint32_t i = 2; i < fs->total_clusters + 2; i++) {
        if (fat_read(fs, i) == FAT32_FREE) {
            fat_write(fs, i, FAT32_EOC);
            return i;
        }
    }
    return 0;
}

static void free_cluster_chain(struct fat32_fs *fs, uint32_t start) {
    while (start >= 2 && start < FAT32_EOC) {
        uint32_t next = fat_read(fs, start);
        fat_write(fs, start, FAT32_FREE);
        start = next;
    }
}

int fat32_mount(struct fat32_fs *fs, int drive, uint32_t partition_lba) {
    memset(fs, 0, sizeof(*fs));
    fs->drive = drive;
    fs->partition_lba = partition_lba;
    fs->fat_cache_sector = 0xFFFFFFFF;
    fs->cached_cluster = 0xFFFFFFFF;
    
    uint8_t sector[512];
    if (ata_read_sectors(drive, partition_lba, 1, sector) < 0)
        return -1;
    
    struct fat32_bpb *bpb = (struct fat32_bpb*)sector;
    
    if (bpb->bytes_per_sector != 512)
        return -1;
    
    if (memcmp(bpb->fs_type, "FAT32   ", 8) != 0)
        return -1;
    
    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->fat_lba = bpb->reserved_sectors;
    fs->fat_size = bpb->fat_size_32;
    fs->data_lba = fs->fat_lba + bpb->fat_count * fs->fat_size;
    fs->root_cluster = bpb->root_cluster;
    
    uint32_t data_sectors = bpb->total_sectors_32 - fs->data_lba;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    
    fs->mounted = true;
    return 0;
}

void fat32_unmount(struct fat32_fs *fs) {
    fs->mounted = false;
}

static void name_to_8_3(const char *name, char *out) {
    memset(out, ' ', 11);
    
    const char *dot = strrchr(name, '.');
    int name_len = dot ? (dot - name) : strlen(name);
    
    for (int i = 0; i < 8 && i < name_len; i++)
        out[i] = (char)toupper((unsigned char)name[i]);
    
    if (dot) {
        dot++;
        for (int i = 0; i < 3 && dot[i]; i++)
            out[8 + i] = (char)toupper((unsigned char)dot[i]);
    }
}

static void name_from_8_3(const char *in, char *out) {
    int j = 0;
    
    for (int i = 0; i < 8 && in[i] != ' '; i++)
        out[j++] = (char)tolower((unsigned char)in[i]);
    
    if (in[8] != ' ') {
        out[j++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++)
            out[j++] = (char)tolower((unsigned char)in[i]);
    }
    
    out[j] = 0;
}

static bool validate_path(const char *path) {
    if (!path || strlen(path) > 255)
        return false;
    
    if (strstr((char*)path, ".."))
        return false;
    
    while (*path) {
        char c = *path++;
        if (c < 32 || c == '<' || c == '>' || c == ':' || 
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static int find_entry(struct fat32_fs *fs, uint32_t dir_cluster, const char *name, struct fat32_dir_entry *entry, uint32_t *entry_cluster, uint32_t *entry_offset) {
    char short_name[12];
    name_to_8_3(name, short_name);
    
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (read_cluster(fs, cluster, fs->cluster_buffer) < 0)
            return -1;
        
        uint32_t entries = fs->sectors_per_cluster * 512 / 32;
        struct fat32_dir_entry *e = (struct fat32_dir_entry*)fs->cluster_buffer;
        
        for (uint32_t i = 0; i < entries; i++, e++) {
            if (e->name[0] == 0) return -1;
            if ((uint8_t)e->name[0] == 0xE5) continue;
            if (e->attr == FAT32_ATTR_LFN) continue;
            
            if (memcmp(e->name, short_name, 11) == 0) {
                memcpy(entry, e, sizeof(*entry));
                if (entry_cluster) *entry_cluster = cluster;
                if (entry_offset) *entry_offset = i;
                return 0;
            }
        }
        
        cluster = fat_read(fs, cluster);
    }
    
    return -1;
}

int fat32_open(struct fat32_fs *fs, const char *path, struct fat32_file *file) {
    if (!fs->mounted || !validate_path(path))
        return -1;
    
    memset(file, 0, sizeof(*file));
    file->fs = fs;
    
    if (path[0] == '/') path++;
    if (!*path) {
        file->start_cluster = fs->root_cluster;
        file->current_cluster = fs->root_cluster;
        file->attr = FAT32_ATTR_DIRECTORY;
        strcpy(file->name, "/");
        return 0;
    }
    
    uint32_t cluster = fs->root_cluster;
    char component[256];
    struct fat32_dir_entry entry;
    
    while (*path) {
        const char *next = strchr(path, '/');
        size_t len = next ? (size_t)(next - path) : strlen(path);
        
        if (len >= sizeof(component))
            return -1;
        
        memcpy(component, path, len);
        component[len] = 0;
        
        if (find_entry(fs, cluster, component, &entry, NULL, NULL) < 0)
            return -1;
        
        cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;
        path = next ? next + 1 : path + len;
    }
    
    file->start_cluster = cluster;
    file->current_cluster = cluster;
    file->size = entry.file_size;
    file->attr = entry.attr;
    name_from_8_3(entry.name, file->name);
    
    return 0;
}

int fat32_read(struct fat32_file *file, void *buffer, size_t size) {
    if (!file->fs->mounted || (file->attr & FAT32_ATTR_DIRECTORY))
        return -1;
    
    if (file->position >= file->size)
        return 0;
    
    if (file->position + size > file->size)
        size = file->size - file->position;
    
    struct fat32_fs *fs = file->fs;
    uint32_t cluster_size = fs->sectors_per_cluster * 512;
    uint8_t *buf = (uint8_t*)buffer;
    size_t read = 0;
    
    while (size > 0 && file->current_cluster >= 2 && file->current_cluster < FAT32_EOC) {
        if (read_cluster(fs, file->current_cluster, fs->cluster_buffer) < 0)
            return read ? read : -1;
        
        uint32_t offset = file->position % cluster_size;
        uint32_t chunk = MIN(size, cluster_size - offset);
        
        memcpy(buf, fs->cluster_buffer + offset, chunk);
        
        buf += chunk;
        size -= chunk;
        read += chunk;
        file->position += chunk;
        
        if (file->position % cluster_size == 0)
            file->current_cluster = fat_read(fs, file->current_cluster);
    }
    
    return read;
}

int fat32_write(struct fat32_file *file, const void *buffer, size_t size) {
    if (!file->fs->mounted || (file->attr & FAT32_ATTR_DIRECTORY))
        return -1;
    
    struct fat32_fs *fs = file->fs;
    uint32_t cluster_size = fs->sectors_per_cluster * 512;
    const uint8_t *buf = (const uint8_t*)buffer;
    size_t written = 0;
    
    while (size > 0) {
        if (file->current_cluster < 2 || file->current_cluster >= FAT32_EOC) {
            uint32_t new_cluster = alloc_cluster(fs);
            if (!new_cluster) return written ? written : -1;
            
            if (file->start_cluster < 2) {
                file->start_cluster = new_cluster;
            } else {
                uint32_t last = file->start_cluster;
                while (1) {
                    uint32_t next = fat_read(fs, last);
                    if (next >= FAT32_EOC) break;
                    last = next;
                }
                fat_write(fs, last, new_cluster);
            }
            file->current_cluster = new_cluster;
            memset(fs->cluster_buffer, 0, cluster_size);
        } else {
            if (read_cluster(fs, file->current_cluster, fs->cluster_buffer) < 0)
                return written ? written : -1;
        }
        
        uint32_t offset = file->position % cluster_size;
        uint32_t chunk = MIN(size, cluster_size - offset);
        
        memcpy(fs->cluster_buffer + offset, buf, chunk);
        
        if (write_cluster(fs, file->current_cluster, fs->cluster_buffer) < 0)
            return written ? written : -1;
        
        buf += chunk;
        size -= chunk;
        written += chunk;
        file->position += chunk;
        
        if (file->position > file->size)
            file->size = file->position;
        
        if (file->position % cluster_size == 0)
            file->current_cluster = fat_read(fs, file->current_cluster);
    }
    
    return written;
}

int fat32_flush_entry(struct fat32_file *file) {
    struct fat32_fs *fs = file->fs;
    if (!fs || !fs->mounted) return -1;

    const char *full_path = file->name;

    const char *path = full_path;
    if (path[0] == '/') path++;

    uint32_t dir_cluster = fs->root_cluster;
    const char *last_component = path;

    while (*path) {
        const char *next = strchr(path, '/');
        if (!next) { last_component = path; break; }

        size_t len = (size_t)(next - path);
        char comp[256];
        if (len >= sizeof(comp)) return -1;
        memcpy(comp, path, len);
        comp[len] = '\0';

        struct fat32_dir_entry e;
        if (find_entry(fs, dir_cluster, comp, &e, NULL, NULL) < 0) return -1;
        dir_cluster = ((uint32_t)e.cluster_hi << 16) | e.cluster_lo;
        path = next + 1;
        last_component = path;
    }

    struct fat32_dir_entry entry;
    uint32_t entry_cluster, entry_offset;
    if (find_entry(fs, dir_cluster, last_component, &entry,
                   &entry_cluster, &entry_offset) < 0)
        return -1;

    if (read_cluster(fs, entry_cluster, fs->cluster_buffer) < 0) return -1;

    struct fat32_dir_entry *e =
        (struct fat32_dir_entry*)fs->cluster_buffer + entry_offset;

    e->file_size  = file->size;
    e->cluster_hi = (uint16_t)(file->start_cluster >> 16);
    e->cluster_lo = (uint16_t)(file->start_cluster & 0xFFFF);

    return write_cluster(fs, entry_cluster, fs->cluster_buffer);
}

int fat32_close(struct fat32_file *file) {
    file->fs = NULL;
    return 0;
}

static int add_dir_entry(struct fat32_fs *fs, uint32_t dir_cluster, const char *name, uint32_t cluster, uint8_t attr, uint32_t size) {
    char short_name[12];
    name_to_8_3(name, short_name);
    
    uint32_t current = dir_cluster;
    while (current >= 2 && current < FAT32_EOC) {
        if (read_cluster(fs, current, fs->cluster_buffer) < 0)
            return -1;
        
        uint32_t entries = fs->sectors_per_cluster * 512 / 32;
        struct fat32_dir_entry *e = (struct fat32_dir_entry*)fs->cluster_buffer;
        
        for (uint32_t i = 0; i < entries; i++, e++) {
            if (e->name[0] == 0 || (uint8_t)e->name[0] == 0xE5) {
                memset(e, 0, sizeof(*e));
                memcpy(e->name, short_name, 11);
                e->attr = attr;
                e->cluster_hi = cluster >> 16;
                e->cluster_lo = cluster & 0xFFFF;
                e->file_size = size;
                return write_cluster(fs, current, fs->cluster_buffer);
            }
        }
        
        uint32_t next = fat_read(fs, current);
        if (next >= FAT32_EOC) {
            next = alloc_cluster(fs);
            if (!next) return -1;
            fat_write(fs, current, next);
            memset(fs->cluster_buffer, 0, fs->sectors_per_cluster * 512);
            write_cluster(fs, next, fs->cluster_buffer);
        }
        current = next;
    }
    
    return -1;
}

int fat32_create(struct fat32_fs *fs, const char *path, uint8_t attr) {
    if (!fs->mounted || !validate_path(path))
        return -1;
    
    if (path[0] == '/') path++;
    
    char dir_path[256] = "";
    const char *name = strrchr(path, '/');
    if (name) {
        size_t len = name - path;
        memcpy(dir_path, path, len);
        dir_path[len] = 0;
        name++;
    } else {
        name = path;
    }
    
    uint32_t dir_cluster = fs->root_cluster;
    if (dir_path[0]) {
        struct fat32_file dir;
        if (fat32_open(fs, dir_path, &dir) < 0)
            return -1;
        if (!(dir.attr & FAT32_ATTR_DIRECTORY))
            return -1;
        dir_cluster = dir.start_cluster;
    }
    
    struct fat32_dir_entry existing;
    if (find_entry(fs, dir_cluster, name, &existing, NULL, NULL) == 0)
        return -1;
    
    uint32_t cluster = 0;
    if (attr & FAT32_ATTR_DIRECTORY) {
        cluster = alloc_cluster(fs);
        if (!cluster) return -1;
        
        memset(fs->cluster_buffer, 0, fs->sectors_per_cluster * 512);
        struct fat32_dir_entry *e = (struct fat32_dir_entry*)fs->cluster_buffer;
        
        memset(e->name, ' ', 11);
        e->name[0] = '.';
        e->attr = FAT32_ATTR_DIRECTORY;
        e->cluster_hi = cluster >> 16;
        e->cluster_lo = cluster & 0xFFFF;
        
        e++;
        memset(e->name, ' ', 11);
        e->name[0] = '.';
        e->name[1] = '.';
        e->attr = FAT32_ATTR_DIRECTORY;
        e->cluster_hi = dir_cluster >> 16;
        e->cluster_lo = dir_cluster & 0xFFFF;
        
        write_cluster(fs, cluster, fs->cluster_buffer);
    }
    
    return add_dir_entry(fs, dir_cluster, name, cluster, attr, 0);
}

int fat32_delete(struct fat32_fs *fs, const char *path) {
    if (!fs->mounted || !validate_path(path))
        return -1;
    
    if (path[0] == '/') path++;
    
    char dir_path[256] = "";
    const char *name = strrchr(path, '/');
    if (name) {
        size_t len = name - path;
        memcpy(dir_path, path, len);
        dir_path[len] = 0;
        name++;
    } else {
        name = path;
    }
    
    uint32_t dir_cluster = fs->root_cluster;
    if (dir_path[0]) {
        struct fat32_file dir;
        if (fat32_open(fs, dir_path, &dir) < 0)
            return -1;
        dir_cluster = dir.start_cluster;
    }
    
    struct fat32_dir_entry entry;
    uint32_t entry_cluster, entry_offset;
    if (find_entry(fs, dir_cluster, name, &entry, &entry_cluster, &entry_offset) < 0)
        return -1;
    
    uint32_t file_cluster = ((uint32_t)entry.cluster_hi << 16) | entry.cluster_lo;
    if (file_cluster >= 2)
        free_cluster_chain(fs, file_cluster);
    
    if (read_cluster(fs, entry_cluster, fs->cluster_buffer) < 0)
        return -1;
    
    struct fat32_dir_entry *e = (struct fat32_dir_entry*)fs->cluster_buffer + entry_offset;
    e->name[0] = 0xE5;
    
    return write_cluster(fs, entry_cluster, fs->cluster_buffer);
}

int fat32_opendir(struct fat32_fs *fs, const char *path, struct fat32_dir *dir) {
    struct fat32_file file;
    if (fat32_open(fs, path, &file) < 0)
        return -1;
    
    if (!(file.attr & FAT32_ATTR_DIRECTORY))
        return -1;
    
    dir->fs = fs;
    dir->cluster = file.start_cluster;
    dir->entry_index = 0;
    return 0;
}

int fat32_readdir(struct fat32_dir *dir, struct fat32_file *file) {
    struct fat32_fs *fs = dir->fs;
    uint32_t entries_per_cluster = fs->sectors_per_cluster * 512 / 32;
    
    while (dir->cluster >= 2 && dir->cluster < FAT32_EOC) {
        if (read_cluster(fs, dir->cluster, fs->cluster_buffer) < 0)
            return -1;
        
        struct fat32_dir_entry *e = (struct fat32_dir_entry*)fs->cluster_buffer;
        e += dir->entry_index % entries_per_cluster;
        
        while (dir->entry_index % entries_per_cluster < entries_per_cluster) {
            if (e->name[0] == 0) return -1;
            
            dir->entry_index++;
            
            if ((uint8_t)e->name[0] != 0xE5 && e->attr != FAT32_ATTR_LFN &&
                e->name[0] != '.') {
                
                memset(file, 0, sizeof(*file));
                file->fs = fs;
                file->start_cluster = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
                file->current_cluster = file->start_cluster;
                file->size = e->file_size;
                file->attr = e->attr;
                name_from_8_3(e->name, file->name);
                return 0;
            }
            
            e++;
        }
        
        dir->cluster = fat_read(fs, dir->cluster);
        dir->entry_index = 0;
    }
    
    return -1;
}

int fat32_mkdir(struct fat32_fs *fs, const char *path) {
    return fat32_create(fs, path, FAT32_ATTR_DIRECTORY);
}

uint32_t fat32_get_free_space(struct fat32_fs *fs) {
    uint32_t free = 0;
    for (uint32_t i = 2; i < fs->total_clusters + 2; i++) {
        if (fat_read(fs, i) == FAT32_FREE)
            free++;
    }
    return free * fs->sectors_per_cluster * 512;
}

uint32_t fat32_get_total_space(struct fat32_fs *fs) {
    return fs->total_clusters * fs->sectors_per_cluster * 512;
}

int fat32_format(int drive, uint32_t partition_lba, uint32_t partition_size, const char *label) {
    if (partition_size < 65536)
        return -1;

    uint8_t sector[512];
    memset(sector, 0, 512);

    struct fat32_bpb *bpb = (struct fat32_bpb*)sector;

    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x58;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem, "QMSOS   ", 8);
    bpb->bytes_per_sector = 512;

    if (partition_size < 532480)
        bpb->sectors_per_cluster = 1;
    else if (partition_size < 16777216)
        bpb->sectors_per_cluster = 8;
    else
        bpb->sectors_per_cluster = 64;

    bpb->reserved_sectors = 32;
    bpb->fat_count = 2;
    bpb->root_entry_count = 0;
    bpb->total_sectors_16 = 0;
    bpb->media_type = 0xF8;
    bpb->fat_size_16 = 0;
    bpb->sectors_per_track = 63;
    bpb->head_count = 255;
    bpb->hidden_sectors = partition_lba;
    bpb->total_sectors_32 = partition_size;

    uint32_t data_sectors = partition_size - 32;
    uint32_t clusters = data_sectors / bpb->sectors_per_cluster;
    bpb->fat_size_32 = (clusters * 4 + 511) / 512;

    bpb->ext_flags = 0;
    bpb->version = 0;
    bpb->root_cluster = 2;
    bpb->fsinfo_sector = 1;
    bpb->backup_boot = 6;
    bpb->drive_number = 0x80;
    bpb->boot_sig = 0x29;
    bpb->volume_id = 0x12345678;

    memset(bpb->volume_label, ' ', 11);
    size_t len = strlen(label);
    if (len > 11) len = 11;
    for (size_t i = 0; i < len; i++)
        bpb->volume_label[i] = toupper(label[i]);

    memcpy(bpb->fs_type, "FAT32   ", 8);

    sector[510] = 0x55;
    sector[511] = 0xAA;

    if (ata_write_sectors(drive, partition_lba, 1, sector) < 0)
        return -1;
    if (ata_write_sectors(drive, partition_lba + 6, 1, sector) < 0)
        return -1;

    memset(sector, 0, 512);
    *(uint32_t*)(sector + 0) = 0x41615252;
    *(uint32_t*)(sector + 484) = 0x61417272;
    *(uint32_t*)(sector + 488) = clusters - 1;
    *(uint32_t*)(sector + 492) = 3;
    *(uint32_t*)(sector + 508) = 0xAA550000;

    if (ata_write_sectors(drive, partition_lba + 1, 1, sector) < 0)
        return -1;

    memset(sector, 0, 512);
    uint32_t fat_lba = partition_lba + 32;
    uint32_t fat_sectors = bpb->fat_size_32;
    
    for (uint32_t i = 0; i < fat_sectors; i++) {
        if (ata_write_sectors(drive, fat_lba + i, 1, sector) < 0)
            return -1;
        if (ata_write_sectors(drive, fat_lba + fat_sectors + i, 1, sector) < 0)
            return -1;
    }

    uint32_t fat[128];
    memset(fat, 0, 512);
    fat[0] = 0x0FFFFFF8;
    fat[1] = 0x0FFFFFFF;
    fat[2] = 0x0FFFFFF8;

    if (ata_write_sectors(drive, fat_lba, 1, fat) < 0)
        return -1;
    if (ata_write_sectors(drive, fat_lba + fat_sectors, 1, fat) < 0)
        return -1;

    uint32_t data_lba = fat_lba + 2 * fat_sectors;
    memset(sector, 0, 512);
    for (uint32_t i = 0; i < bpb->sectors_per_cluster; i++) {
        if (ata_write_sectors(drive, data_lba + i, 1, sector) < 0)
            return -1;
    }
    return 0;
}
