#include "fat16.h"
#include <lib/strtools/string.h>
#include <io/printg.h>

static void dfat_memset(void* d, int c, size_t n) {
    unsigned char* p = (unsigned char*)d;
    while (n--) *p++ = (unsigned char)c;
}

static void dfat_memcpy(void* d, const void* s, size_t n) {
    unsigned char* dp = (unsigned char*)d;
    const unsigned char* sp = (const unsigned char*)s;
    while (n--) *dp++ = *sp++;
}

static bool dfat_streq_ci(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

static char dfat_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static uint16_t le16r(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32r(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void le16w(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void le32w(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

DFS_Fat16::DFS_Fat16(IDiskIO* disk, uint32_t lba_start)
    : disk_(disk), part_lba_(lba_start),
      bytes_per_sector_(DFAT_SECTOR_SIZE),
      sectors_per_cluster_(0), reserved_sectors_(0),
      num_fats_(0), root_entry_count_(0), fat_size_sectors_(0),
      fat_lba_(0), root_lba_(0), data_lba_(0), total_sectors_(0),
      mounted_(false), inode_count_(0), fat_cached_lba_(0xFFFFFFFF),
      fat_dirty_(false)
{
    dfat_memset(inodes_,     0, sizeof(inodes_));
    dfat_memset(sector_buf_, 0, sizeof(sector_buf_));
    dfat_memset(fat_buf_,    0, sizeof(fat_buf_));
}

FS_Error DFS_Fat16::mount() {
    if (!disk_) return FS_ERR_BAD_ARG;

    if (!disk_->read_sectors(part_lba_, sector_buf_, 1))
        return FS_ERR_IO;

    uint8_t first_byte = sector_buf_[0];
    bool looks_like_mbr =
        (sector_buf_[510] == 0x55 && sector_buf_[511] == 0xAA) &&
        (first_byte != 0xEB && first_byte != 0xE9);

    if (looks_like_mbr && part_lba_ == 0) {
        for (int e = 0; e < 4; e++) {
            const uint8_t* pe = sector_buf_ + 0x1BE + e * 16;
            uint8_t ptype = pe[4];
            if (ptype == 0x04 || ptype == 0x06 || ptype == 0x0E) {
                part_lba_ = le32r(pe + 8);
                log(3, "fat16: partition at LBA %d", (int)part_lba_);
                break;
            }
        }
        if (!disk_->read_sectors(part_lba_, sector_buf_, 1))
            return FS_ERR_IO;
    }

    const uint8_t* bpb = sector_buf_;
    bytes_per_sector_    = le16r(bpb + 11);
    sectors_per_cluster_ = bpb[13];
    reserved_sectors_    = le16r(bpb + 14);
    num_fats_            = bpb[16];
    root_entry_count_    = le16r(bpb + 17);
    uint16_t tot16       = le16r(bpb + 19);
    fat_size_sectors_    = le16r(bpb + 22);
    uint32_t tot32       = le32r(bpb + 32);
    total_sectors_       = tot16 ? tot16 : tot32;

    if (bytes_per_sector_ == 0 || sectors_per_cluster_ == 0 ||
        fat_size_sectors_ == 0 || num_fats_ == 0) {
        log(1, "fat16: invalid BPB");
        return FS_ERR_IO;
    }

    fat_lba_  = part_lba_ + reserved_sectors_;
    root_lba_ = fat_lba_  + (uint32_t)num_fats_ * fat_size_sectors_;
    uint32_t root_sectors = ((uint32_t)root_entry_count_ * 32 + bytes_per_sector_ - 1) / bytes_per_sector_;
    data_lba_ = root_lba_ + root_sectors;

    log(3, "fat16: BPS=%d SPC=%d FAT@%d ROOT@%d DATA@%d", (int)bytes_per_sector_, (int)sectors_per_cluster_, (int)fat_lba_, (int)root_lba_, (int)data_lba_);

    mounted_     = true;
    inode_count_ = 0;
    dfat_memset(inodes_, 0, sizeof(inodes_));

    FS_Error err = scan_dir(-1, 0);
    if (err != FS_OK) { mounted_ = false; return err; }
    return FS_OK;
}

FS_Error DFS_Fat16::unmount() {
    flush_fat();
    mounted_     = false;
    inode_count_ = 0;
    return FS_OK;
}

uint32_t DFS_Fat16::cluster_to_lba(uint16_t cluster) const {
    return data_lba_ + (uint32_t)(cluster - 2) * sectors_per_cluster_;
}

uint16_t DFS_Fat16::fat_entry(uint16_t cluster) {
    uint32_t byte_offset   = (uint32_t)cluster * 2;
    uint32_t sector_in_fat = byte_offset / bytes_per_sector_;
    uint32_t lba           = fat_lba_ + sector_in_fat;
    if (!load_fat_sector(lba)) return 0;
    uint32_t off = byte_offset % bytes_per_sector_;
    return le16r(fat_buf_ + off);
}

bool DFS_Fat16::load_fat_sector(uint32_t lba) {
    if (lba == fat_cached_lba_) return true;
    if (fat_dirty_) flush_fat();
    if (!disk_->read_sectors(lba, fat_buf_, 1)) return false;
    fat_cached_lba_ = lba;
    fat_dirty_ = false;
    return true;
}

void DFS_Fat16::flush_fat() {
    if (!fat_dirty_ || fat_cached_lba_ == 0xFFFFFFFF) return;
    for (uint8_t f = 0; f < num_fats_; f++) {
        uint32_t off = fat_cached_lba_ - fat_lba_;
        uint32_t lba = fat_lba_ + f * fat_size_sectors_ + off;
        disk_->write_sectors(lba, fat_buf_, 1);
    }
    fat_dirty_ = false;
}

bool DFS_Fat16::set_fat_entry(uint16_t cluster, uint16_t value) {
    uint32_t byte_offset   = (uint32_t)cluster * 2;
    uint32_t sector_in_fat = byte_offset / bytes_per_sector_;
    uint32_t lba           = fat_lba_ + sector_in_fat;
    if (!load_fat_sector(lba)) return false;
    uint32_t off = byte_offset % bytes_per_sector_;
    le16w(fat_buf_ + off, value);
    fat_dirty_ = true;
    return true;
}

uint16_t DFS_Fat16::alloc_cluster() {
    uint32_t max_cluster = (total_sectors_ - (data_lba_ - part_lba_)) / sectors_per_cluster_ + 2;
    if (max_cluster > 0xFFF6) max_cluster = 0xFFF6;

    for (uint16_t c = 2; c < (uint16_t)max_cluster; c++) {
        if (fat_entry(c) == DFAT_FREE_CLUSTER) {
            set_fat_entry(c, 0xFFFF);
            flush_fat();
            return c;
        }
    }
    return 0;
}

void DFS_Fat16::free_cluster_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < DFAT_FAT16_EOC) {
        uint16_t next = fat_entry(cluster);
        set_fat_entry(cluster, DFAT_FREE_CLUSTER);
        cluster = next;
    }
    flush_fat();
}

int DFS_Fat16::alloc_inode(const char* name, uint8_t type, int16_t parent_id, uint16_t first_cluster, uint32_t size, uint32_t dir_lba, uint32_t dir_entry_idx) {
    if (inode_count_ >= DFAT_INODE_COUNT) return -1;
    int id = inode_count_++;
    DFatInode& n = inodes_[id];
    dfat_memset(&n, 0, sizeof(DFatInode));
    strncpy(n.name, name, FS_MAX_NAME_LEN - 1);
    n.name[FS_MAX_NAME_LEN - 1] = '\0';
    n.type          = type;
    n.parent_id     = parent_id;
    n.first_cluster = first_cluster;
    n.size          = size;
    n.populated     = false;
    n.dir_lba       = dir_lba;
    n.dir_entry_idx = dir_entry_idx;
    return id;
}

void DFS_Fat16::format_83(const char raw[11], char out[FS_MAX_NAME_LEN]) {
    int oi = 0;
    int name_len = 8;
    while (name_len > 0 && raw[name_len - 1] == ' ') name_len--;
    for (int i = 0; i < name_len && oi < FS_MAX_NAME_LEN - 1; i++)
        out[oi++] = dfat_upper(raw[i]);
    int ext_len = 3;
    while (ext_len > 0 && raw[8 + ext_len - 1] == ' ') ext_len--;
    if (ext_len > 0 && oi < FS_MAX_NAME_LEN - 2) {
        out[oi++] = '.';
        for (int i = 0; i < ext_len && oi < FS_MAX_NAME_LEN - 1; i++)
            out[oi++] = dfat_upper(raw[8 + i]);
    }
    out[oi] = '\0';
}

static void to_83(const char* name, char out[11]) {
    dfat_memset(out, ' ', 11);
    int ni = 0;
    const char* p = name;
    while (*p && *p != '.' && ni < 8) {
        char c = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
        out[ni++] = c;
        p++;
    }
    if (*p == '.') {
        p++;
        int ei = 0;
        while (*p && ei < 3) {
            char c = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
            out[8 + ei++] = c;
            p++;
        }
    }
}

FS_Error DFS_Fat16::scan_dir(int parent_id, uint16_t first_cluster) {
    const uint32_t ENTRIES_PER_SECTOR = bytes_per_sector_ / 32;
    if (parent_id >= 0 && parent_id < inode_count_)
        inodes_[parent_id].populated = true;

    if (first_cluster == 0) {
        uint32_t root_sectors = ((uint32_t)root_entry_count_ * 32 + bytes_per_sector_ - 1) / bytes_per_sector_;
        uint32_t entries_left = root_entry_count_;

        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            uint32_t lba = root_lba_ + sec;
            if (!disk_->read_sectors(lba, sector_buf_, 1))
                return FS_ERR_IO;

            uint32_t n = (entries_left < ENTRIES_PER_SECTOR) ? entries_left : ENTRIES_PER_SECTOR;
            for (uint32_t e = 0; e < n; e++) {
                const uint8_t* de = sector_buf_ + e * 32;
                if (de[0] == 0x00) return FS_OK;
                if (de[0] == 0xE5) continue;
                uint8_t attr = de[11];
                if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME_ID)) continue;

                char name83[FS_MAX_NAME_LEN];
                format_83((const char*)de, name83);
                if (name83[0] == '\0') continue;

                uint8_t  type  = (attr & FAT_ATTR_DIRECTORY) ? ENT_DIR : ENT_FILE;
                uint16_t clust = le16r(de + 26);
                uint32_t fsz   = le32r(de + 28);
                alloc_inode(name83, type, (int16_t)parent_id, clust, fsz, lba, e);
            }
            entries_left -= n;
        }
    } else {
        uint16_t cluster = first_cluster;
        while (cluster >= 2 && cluster < DFAT_FAT16_EOC) {
            uint32_t lba = cluster_to_lba(cluster);
            for (uint8_t sec = 0; sec < sectors_per_cluster_; sec++) {
                uint32_t slba = lba + sec;
                if (!disk_->read_sectors(slba, sector_buf_, 1))
                    return FS_ERR_IO;

                for (uint32_t e = 0; e < ENTRIES_PER_SECTOR; e++) {
                    const uint8_t* de = sector_buf_ + e * 32;
                    if (de[0] == 0x00) return FS_OK;
                    if (de[0] == 0xE5) continue;
                    if (de[0] == '.') continue;

                    uint8_t attr = de[11];
                    if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME_ID)) continue;

                    char name83[FS_MAX_NAME_LEN];
                    format_83((const char*)de, name83);
                    if (name83[0] == '\0') continue;

                    uint8_t  type  = (attr & FAT_ATTR_DIRECTORY) ? ENT_DIR : ENT_FILE;
                    uint16_t clust = le16r(de + 26);
                    uint32_t fsz   = le32r(de + 28);
                    alloc_inode(name83, type, (int16_t)parent_id, clust, fsz, slba, e);
                }
            }
            cluster = fat_entry(cluster);
        }
    }
    return FS_OK;
}

FS_Error DFS_Fat16::ensure_populated(int parent_id) {
    if (parent_id == -1) return FS_OK;
    if (parent_id < 0 || parent_id >= inode_count_) return FS_ERR_NOT_FOUND;
    DFatInode& nd = inodes_[parent_id];
    if (nd.populated) return FS_OK;
    if (nd.type != ENT_DIR) return FS_ERR_NOT_DIR;
    return scan_dir(parent_id, nd.first_cluster);
}

FS_ListResult DFS_Fat16::list(int parent_id) {
    FS_ListResult r; r.count = 0; r.error = FS_OK;
    if (!mounted_) { r.error = FS_ERR_NO_FS; return r; }
    ensure_populated(parent_id);
    for (int i = 0; i < inode_count_ && r.count < FS_MAX_ENTRIES; i++) {
        if (inodes_[i].type == ENT_NONE) continue;
        if ((int)inodes_[i].parent_id != parent_id) continue;
        FS_DirEntry& e = r.entries[r.count];
        strncpy(e.name, inodes_[i].name, FS_MAX_NAME_LEN - 1);
        e.name[FS_MAX_NAME_LEN - 1] = '\0';
        e.size = inodes_[i].size;
        e.type = inodes_[i].type;
        r.count++;
    }
    return r;
}

int DFS_Fat16::find(const char* name, int parent_id) {
    if (!mounted_ || !name || !*name) return -1;
    ensure_populated(parent_id);
    for (int i = 0; i < inode_count_; i++) {
        if (inodes_[i].type == ENT_NONE) continue;
        if ((int)inodes_[i].parent_id != parent_id) continue;
        if (dfat_streq_ci(inodes_[i].name, name)) return i;
    }
    return -1;
}

int      DFS_Fat16::get_parent(int id) { return (id < 0 || id >= inode_count_) ? -1 : (int)inodes_[id].parent_id; }
const char* DFS_Fat16::get_name(int id) { return (id < 0 || id >= inode_count_) ? "" : inodes_[id].name; }
uint8_t  DFS_Fat16::get_type(int id)   { return (id < 0 || id >= inode_count_) ? ENT_NONE : inodes_[id].type; }
uint32_t DFS_Fat16::get_size(int id)   { return (id < 0 || id >= inode_count_) ? 0 : inodes_[id].size; }

bool DFS_Fat16::write_dir_entry(int id) {
    if (id < 0 || id >= inode_count_) return false;
    DFatInode& n = inodes_[id];

    if (!disk_->read_sectors(n.dir_lba, sector_buf_, 1)) return false;

    uint8_t* de = sector_buf_ + n.dir_entry_idx * 32;

    le16w(de + 26, n.first_cluster);
    le32w(de + 28, n.size);

    return disk_->write_sectors(n.dir_lba, sector_buf_, 1);
}

FS_Error DFS_Fat16::read(int id, void* buf, uint32_t* inout_size) {
    if (!buf || !inout_size)           return FS_ERR_BAD_ARG;
    if (!mounted_)                     return FS_ERR_NO_FS;
    if (id < 0 || id >= inode_count_) return FS_ERR_NOT_FOUND;
    if (inodes_[id].type != ENT_FILE)  return FS_ERR_NOT_FILE;

    uint32_t file_size = inodes_[id].size;
    uint32_t to_read   = (*inout_size < file_size) ? *inout_size : file_size;
    *inout_size = 0;
    if (to_read == 0) return FS_OK;

    uint8_t* dst     = (uint8_t*)buf;
    uint32_t remaining = to_read;
    uint16_t cluster   = inodes_[id].first_cluster;

    while (remaining > 0 && cluster >= 2 && cluster < DFAT_FAT16_EOC) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t sec = 0; sec < sectors_per_cluster_ && remaining > 0; sec++) {
            if (!disk_->read_sectors(lba + sec, sector_buf_, 1))
                return FS_ERR_IO;
            uint32_t copy = (remaining < bytes_per_sector_) ? remaining : bytes_per_sector_;
            dfat_memcpy(dst, sector_buf_, copy);
            dst       += copy;
            remaining -= copy;
        }
        cluster = fat_entry(cluster);
    }

    *inout_size = to_read - remaining;
    return FS_OK;
}

FS_Error DFS_Fat16::write(int id, const void* buf, uint32_t size) {
    if (!buf)                          return FS_ERR_BAD_ARG;
    if (!mounted_)                     return FS_ERR_NO_FS;
    if (id < 0 || id >= inode_count_)  return FS_ERR_NOT_FOUND;
    if (inodes_[id].type != ENT_FILE)  return FS_ERR_NOT_FILE;

    DFatInode& n = inodes_[id];

    if (n.first_cluster == 0 || n.first_cluster >= DFAT_FAT16_EOC) {
        uint16_t c = alloc_cluster();
        if (!c) return FS_ERR_NO_SPACE;
        n.first_cluster = c;
    } else {
        free_cluster_chain(n.first_cluster);
        uint16_t c = alloc_cluster();
        if (!c) return FS_ERR_NO_SPACE;
        n.first_cluster = c;
    }

    const uint8_t* src       = (const uint8_t*)buf;
    uint32_t       remaining  = size;
    uint16_t       cluster    = n.first_cluster;

    while (remaining > 0) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t sec = 0; sec < sectors_per_cluster_ && remaining > 0; sec++) {
            uint32_t copy = (remaining < bytes_per_sector_) ? remaining : bytes_per_sector_;
            dfat_memset(sector_buf_, 0, bytes_per_sector_);
            dfat_memcpy(sector_buf_, src, copy);
            if (!disk_->write_sectors(lba + sec, sector_buf_, 1))
                return FS_ERR_IO;
            src       += copy;
            remaining -= copy;
        }

        if (remaining > 0) {
            uint16_t next = alloc_cluster();
            if (!next) return FS_ERR_NO_SPACE;
            set_fat_entry(cluster, next);
            flush_fat();
            cluster = next;
        }
    }

    n.size = size;
    write_dir_entry(id);
    return FS_OK;
}

FS_Error DFS_Fat16::create(const char* name, uint8_t type, int parent_id) {
    if (!name || !*name) return FS_ERR_BAD_ARG;
    if (type != ENT_FILE && type != ENT_DIR) return FS_ERR_BAD_ARG;
    if (!mounted_) return FS_ERR_NO_FS;

    ensure_populated(parent_id);
    if (find(name, parent_id) != -1) return FS_ERR_EXISTS;

    char raw83[11];
    to_83(name, raw83);

    const uint32_t ENTRIES_PER_SECTOR = bytes_per_sector_ / 32;

    if (parent_id == -1) {
        uint32_t root_sectors = ((uint32_t)root_entry_count_ * 32 + bytes_per_sector_ - 1) / bytes_per_sector_;
        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            uint32_t lba = root_lba_ + sec;
            if (!disk_->read_sectors(lba, sector_buf_, 1))
                return FS_ERR_IO;

            for (uint32_t e = 0; e < ENTRIES_PER_SECTOR; e++) {
                uint8_t* de = sector_buf_ + e * 32;
                if (de[0] == 0x00 || de[0] == 0xE5) {
                    dfat_memset(de, 0, 32);
                    dfat_memcpy(de, raw83, 11);
                    de[11] = (type == ENT_DIR) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
                    if (!disk_->write_sectors(lba, sector_buf_, 1))
                        return FS_ERR_IO;

                    int new_id = alloc_inode(name, type, (int16_t)parent_id, 0, 0, lba, e);
                    if (new_id < 0) return FS_ERR_NO_SPACE;
                    return FS_OK;
                }
            }
        }
        return FS_ERR_NO_SPACE;

    } else {
        DFatInode& par = inodes_[parent_id];
        uint16_t cluster = par.first_cluster;

        while (cluster >= 2 && cluster < DFAT_FAT16_EOC) {
            uint32_t lba = cluster_to_lba(cluster);
            for (uint8_t sec = 0; sec < sectors_per_cluster_; sec++) {
                uint32_t slba = lba + sec;
                if (!disk_->read_sectors(slba, sector_buf_, 1))
                    return FS_ERR_IO;

                for (uint32_t e = 0; e < ENTRIES_PER_SECTOR; e++) {
                    uint8_t* de = sector_buf_ + e * 32;
                    if (de[0] == 0x00 || de[0] == 0xE5) {
                        dfat_memset(de, 0, 32);
                        dfat_memcpy(de, raw83, 11);
                        de[11] = (type == ENT_DIR) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
                        if (!disk_->write_sectors(slba, sector_buf_, 1))
                            return FS_ERR_IO;

                        int new_id = alloc_inode(name, type, (int16_t)parent_id,
                                                 0, 0, slba, e);
                        if (new_id < 0) return FS_ERR_NO_SPACE;
                        return FS_OK;
                    }
                }
            }
            uint16_t next_c = fat_entry(cluster);
            if (next_c >= DFAT_FAT16_EOC) {
                uint16_t new_c = alloc_cluster();
                if (!new_c) return FS_ERR_NO_SPACE;
                set_fat_entry(cluster, new_c);
                flush_fat();
                dfat_memset(sector_buf_, 0, bytes_per_sector_);
                uint32_t new_lba = cluster_to_lba(new_c);
                for (uint8_t s = 0; s < sectors_per_cluster_; s++)
                    disk_->write_sectors(new_lba + s, sector_buf_, 1);
                cluster = new_c;
            } else {
                cluster = next_c;
            }
        }
        return FS_ERR_NO_SPACE;
    }
}

FS_Error DFS_Fat16::remove(int id) {
    if (id < 0 || id >= inode_count_ || inodes_[id].type == ENT_NONE)
        return FS_ERR_NOT_FOUND;

    DFatInode& n = inodes_[id];

    if (n.type == ENT_DIR) {
        FS_ListResult ch = list(id);
        if (ch.count > 0) return FS_ERR_NOT_FOUND;
    }

    if (n.first_cluster >= 2 && n.first_cluster < DFAT_FAT16_EOC)
        free_cluster_chain(n.first_cluster);

    if (disk_->read_sectors(n.dir_lba, sector_buf_, 1)) {
        sector_buf_[n.dir_entry_idx * 32] = 0xE5;
        disk_->write_sectors(n.dir_lba, sector_buf_, 1);
    }

    n.type = ENT_NONE;
    return FS_OK;
}
