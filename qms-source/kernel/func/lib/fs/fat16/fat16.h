#ifndef _FAT16_H
#define _FAT16_H

#include <func/vfs.h>

#define DFAT_INODE_COUNT   128
#define DFAT_SECTOR_SIZE   512
#define DFAT_FAT16_EOC     0xFFF8
#define DFAT_FREE_CLUSTER  0x0000
#define DFAT_BAD_CLUSTER   0xFFF7
#define FS_MAX_NAME_LEN    64
#define FS_MAX_ENTRIES     128

#define FAT_ATTR_READONLY  0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

#define ENT_NONE  0
#define ENT_FILE  1
#define ENT_DIR   2

typedef enum {
    FS_OK              =  0,
    FS_ERR_NOT_FOUND   = -1,
    FS_ERR_EXISTS      = -2,
    FS_ERR_NO_SPACE    = -3,
    FS_ERR_NOT_FILE    = -4,
    FS_ERR_NOT_DIR     = -5,
    FS_ERR_NO_FS       = -6,
    FS_ERR_BAD_ARG     = -7,
    FS_ERR_IO          = -8,
    FS_ERR_TOO_BIG     = -9,
    FS_ERR_NOT_EMPTY   = -10,
} FS_Error;

struct FS_DirEntry {
    char     name[FS_MAX_NAME_LEN];
    uint32_t size;
    uint8_t  type;
    uint8_t  attributes;
};

struct FS_ListResult {
    FS_DirEntry entries[FS_MAX_ENTRIES];
    int         count;
    FS_Error    error;
};

class IFileSystem {
public:
    virtual FS_Error      mount  () = 0;
    virtual FS_Error      unmount() = 0;

    virtual FS_ListResult list      (int parent_id)                      = 0;
    virtual int           find      (const char* name, int parent_id)    = 0;
    virtual int           get_parent(int id)                             = 0;
    virtual const char*   get_name  (int id)                             = 0;
    virtual uint8_t       get_type  (int id)                             = 0;
    virtual uint32_t      get_size  (int id)                             = 0;

    virtual FS_Error      create(const char* name, uint8_t type, int parent_id) = 0;
    virtual FS_Error      remove(int id)                                 = 0;
    virtual FS_Error      read (int id, void* buf, uint32_t* inout_size) = 0;
    virtual FS_Error      write(int id, const void* buf, uint32_t size)  = 0;

    virtual ~IFileSystem() {}
};

struct DFatInode {
    char     name[FS_MAX_NAME_LEN];
    uint32_t size;
    uint8_t  type;
    uint8_t  attributes;
    int16_t  parent_id;
    uint16_t first_cluster;
    bool     populated;
    uint32_t dir_lba;
    uint32_t dir_entry_idx;
} __attribute__((packed));

class DFS_Fat16 : public IFileSystem {
public:
    DFS_Fat16(IDiskIO* disk, uint32_t lba_start = 0);

    FS_Error      mount  () override;
    FS_Error      unmount() override;

    FS_ListResult list      (int parent_id)              override;
    int           find (const char* name, int parent_id) override;
    int           get_parent(int id)                     override;
    const char*   get_name  (int id)                     override;
    uint8_t       get_type  (int id)                     override;
    uint32_t      get_size  (int id)                     override;
    uint8_t       get_attributes(int id) const;
    FS_Error      set_attributes(int id, uint8_t attributes);

    FS_Error      create(const char* name, uint8_t type, int parent_id) override;
    FS_Error      remove(int id)                                 override;
    FS_Error      read (int id, void* buf, uint32_t* inout_size) override;
    FS_Error      write(int id, const void* buf, uint32_t size)  override;

private:
    IDiskIO*  disk_;
    uint32_t  part_lba_;

    uint16_t  bytes_per_sector_;
    uint8_t   sectors_per_cluster_;
    uint16_t  reserved_sectors_;
    uint8_t   num_fats_;
    uint16_t  root_entry_count_;
    uint16_t  fat_size_sectors_;

    uint32_t  fat_lba_;
    uint32_t  root_lba_;
    uint32_t  data_lba_;
    uint32_t  total_sectors_;

    bool      mounted_;

    DFatInode inodes_[DFAT_INODE_COUNT];
    int       inode_count_;

    uint8_t   fat_buf_[DFAT_SECTOR_SIZE];
    uint32_t  fat_cached_lba_;
    bool      fat_dirty_;

    uint8_t   sector_buf_[DFAT_SECTOR_SIZE];

    uint32_t cluster_to_lba(uint16_t cluster) const;

    uint16_t fat_entry    (uint16_t cluster);
    bool     set_fat_entry(uint16_t cluster, uint16_t value);
    bool     load_fat_sector(uint32_t lba);
    void     flush_fat    ();
    uint16_t alloc_cluster();
    void     free_cluster_chain(uint16_t cluster);

    int  alloc_inode(const char* name, uint8_t type, int16_t parent_id, uint16_t first_cluster, uint32_t size, uint32_t dir_lba, uint32_t dir_entry_idx, uint8_t attributes = 0);

    FS_Error scan_dir        (int parent_id, uint16_t first_cluster);
    FS_Error ensure_populated(int parent_id);
    bool     write_dir_entry (int id);

    static void format_83(const char raw[11], char out[FS_MAX_NAME_LEN]);
};

#endif
