#include "vfs_fat32.h"

#include <lib/rmem/memory.h>
#include <lib/strtools/string.h>

#include <drivers/hw/ata_diskio.h>

#include <io/printg.h>

#define FAT32_MAX_NODES 64

struct Fat32Node {
    bool      used;
    bool      is_dir;
    char      path[VFS_MAX_PATH];
    uint32_t  size;
};

struct Fat32Private {
    struct fat32_fs fs;
    IDiskIO*        disk;
    uint32_t        lba_start;
    bool            mounted;

    Fat32Node nodes[FAT32_MAX_NODES];
};

static int fat32_alloc_node(Fat32Private* priv) {
    for (int i = 0; i < FAT32_MAX_NODES; i++) {
        if (!priv->nodes[i].used) return i;
    }
    return -1;
}

static void fat32_free_node(Fat32Private* priv, int id) {
    if (id >= 0 && id < FAT32_MAX_NODES)
        priv->nodes[id].used = false;
}

static int fat32_node_for_path(Fat32Private* priv, const char* fspath) {
    for (int i = 0; i < FAT32_MAX_NODES; i++) {
        if (priv->nodes[i].used && strcmp(priv->nodes[i].path, fspath) == 0) {
            struct fat32_file ftmp;
            if (fat32_open(&priv->fs, fspath, &ftmp) == 0) {
                priv->nodes[i].size = ftmp.size;
                fat32_close(&ftmp);
            }
            return i;
        }
    }

    struct fat32_file f;
    int ret = fat32_open(&priv->fs, fspath, &f);
    if (ret != 0) return -1;

    int id = fat32_alloc_node(priv);
    if (id < 0) return -1;

    Fat32Node& n = priv->nodes[id];
    n.used   = true;
    n.is_dir = (f.attr & FAT32_ATTR_DIRECTORY) != 0;
    n.size   = f.size;
    strncpy(n.path, fspath, VFS_MAX_PATH - 1);
    n.path[VFS_MAX_PATH - 1] = '\0';

    fat32_close(&f);
    return id;
}

static VFS_Error fat32_mount_impl(VFS_FSInstance* inst, void* addr, size_t size, IDiskIO* disk, uint32_t lba_start) {
    (void)addr; (void)size;

    if (!disk) {
        log(1, "FAT32: no disk provided");
        return VFS_ERR_BAD_ARG;
    }

    extern MemoryManager* kernel_mem_manager;
    Fat32Private* priv = (Fat32Private*)kernel_mem_manager->allocate(sizeof(Fat32Private));
    if (!priv) return VFS_ERR_NO_SPACE;
    inst->priv = priv;

    memset(priv, 0, sizeof(Fat32Private));
    priv->disk      = disk;
    priv->lba_start = lba_start;
    priv->mounted   = false;

    int ret = fat32_mount(&priv->fs, 0, lba_start);
    if (ret != 0) {
        log(1, "FAT32: mount failed at LBA %u", lba_start);
        return VFS_ERR_IO;
    }

    priv->mounted = true;
    log(3, "FAT32: mounted | Root cluster: %u | Clusters: %u | Size: ~%u MB", priv->fs.root_cluster, priv->fs.total_clusters, (priv->fs.total_clusters * priv->fs.sectors_per_cluster * 512) / (1024*1024));
    return VFS_OK;
}

static VFS_Error fat32_unmount_impl(VFS_FSInstance* inst) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (priv) {
        if (priv->mounted) {
            fat32_unmount(&priv->fs);
            priv->mounted = false;
        }
        extern MemoryManager* kernel_mem_manager;
        kernel_mem_manager->free(priv);
        inst->priv = nullptr;
    }
    return VFS_OK;
}

static int fat32_lookup_impl(VFS_FSInstance* inst, const char* name, int parent_id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted || !name) return -1;

    char fspath[VFS_MAX_PATH];

    if (parent_id == -1) {
        fspath[0] = '/';
        strncpy(fspath + 1, name, VFS_MAX_PATH - 2);
        fspath[VFS_MAX_PATH - 1] = '\0';
    } else {
        if (parent_id < 0 || parent_id >= FAT32_MAX_NODES ||
            !priv->nodes[parent_id].used) return -1;

        const char* ppath = priv->nodes[parent_id].path;
        int plen = strlen(ppath);
        if (plen + 1 + (int)strlen(name) >= VFS_MAX_PATH) return -1;

        if (ppath[plen - 1] == '/') {
            strncpy(fspath, ppath, VFS_MAX_PATH - 1);
        } else {
            strncpy(fspath, ppath, VFS_MAX_PATH - 1);
            fspath[plen] = '/';
            fspath[plen + 1] = '\0';
        }
        strncat(fspath, name, VFS_MAX_PATH - strlen(fspath) - 1);
    }

    return fat32_node_for_path(priv, fspath);
}

static VFS_Dir fat32_readdir_impl(VFS_FSInstance* inst, int dir_id) {
    VFS_Dir d;
    d.count = 0;
    d.error = VFS_OK;

    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted) {
        d.error = VFS_ERR_NO_FS;
        return d;
    }

    const char* dirpath = "/";
    if (dir_id != -1) {
        if (dir_id < 0 || dir_id >= FAT32_MAX_NODES ||
            !priv->nodes[dir_id].used) {
            d.error = VFS_ERR_NOT_DIR;
            return d;
        }
        dirpath = priv->nodes[dir_id].path;
    }

    struct fat32_dir fdir;
    if (fat32_opendir(&priv->fs, dirpath, &fdir) != 0) {
        d.error = VFS_ERR_NOT_DIR;
        return d;
    }

    struct fat32_file ffile;
    while (fat32_readdir(&fdir, &ffile) == 0) {
        if (d.count >= VFS_MAX_DIR_ENTRIES) break;

        if (ffile.name[0] == '.' &&
            (ffile.name[1] == '\0' ||
             (ffile.name[1] == '.' && ffile.name[2] == '\0')))
            continue;

        strncpy(d.entries[d.count].name, ffile.name, VFS_MAX_NAME - 1);
        d.entries[d.count].name[VFS_MAX_NAME - 1] = '\0';
        d.entries[d.count].size = ffile.size;
        d.entries[d.count].type = (ffile.attr & FAT32_ATTR_DIRECTORY) ? VFS_ENT_DIR : VFS_ENT_FILE;
        d.count++;
    }

    return d;
}

static uint8_t fat32_type_impl(VFS_FSInstance* inst, int id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return VFS_ENT_NONE;
    return priv->nodes[id].is_dir ? VFS_ENT_DIR : VFS_ENT_FILE;
}

static uint32_t fat32_size_impl(VFS_FSInstance* inst, int id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return 0;
    return priv->nodes[id].size;
}

static const char* fat32_name_impl(VFS_FSInstance* inst, int id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return "";
    const char* p = priv->nodes[id].path;
    const char* last = p;
    for (const char* s = p; *s; s++)
        if (*s == '/') last = s + 1;
    return last;
}

static int fat32_parent_impl(VFS_FSInstance* inst, int id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return -1;

    const char* path = priv->nodes[id].path;

    int len = strlen(path);
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') slash--;

    if (slash == 0) return -1;

    char parent_path[VFS_MAX_PATH];
    strncpy(parent_path, path, slash);
    parent_path[slash] = '\0';

    return fat32_node_for_path(priv, parent_path);
}

static VFS_Error fat32_read_impl(VFS_FSInstance* inst, int id, void* buf, uint32_t* inout_size) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted || !buf || !inout_size)
        return VFS_ERR_BAD_ARG;

    if (id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    Fat32Node& n = priv->nodes[id];
    if (n.is_dir) return VFS_ERR_NOT_FILE;

    struct fat32_file file;
    if (fat32_open(&priv->fs, n.path, &file) != 0) {
        log(1, "FAT32 read: open failed for '%s'", n.path);
        return VFS_ERR_NOT_FOUND;
    }

    uint32_t to_read = (*inout_size < n.size) ? *inout_size : n.size;
    int rd = fat32_read(&file, buf, to_read);
    fat32_close(&file);

    if (rd < 0) return VFS_ERR_IO;

    *inout_size = (uint32_t)rd;
    return VFS_OK;
}

extern "C" int fat32_flush_entry(struct fat32_file* file);

static VFS_Error fat32_write_impl(VFS_FSInstance* inst, int id, const void* buf, uint32_t size) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted || !buf)
        return VFS_ERR_BAD_ARG;

    if (id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    Fat32Node& n = priv->nodes[id];
    if (n.is_dir) return VFS_ERR_NOT_FILE;

    struct fat32_file file;
    if (fat32_open(&priv->fs, n.path, &file) != 0)
        return VFS_ERR_NOT_FOUND;

    int wr = fat32_write(&file, buf, size);

    if (wr >= 0) {
        strncpy(file.name, n.path, sizeof(file.name) - 1);
        file.name[sizeof(file.name) - 1] = '\0';

        fat32_flush_entry(&file);
        n.size = file.size;
    }

    fat32_close(&file);
    return (wr >= 0) ? VFS_OK : VFS_ERR_IO;
}

static VFS_Error fat32_create_impl(VFS_FSInstance* inst, const char* name, uint8_t type, int parent_id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted) return VFS_ERR_NO_FS;

    char fspath[VFS_MAX_PATH];
    if (parent_id == -1) {
        fspath[0] = '/';
        strncpy(fspath + 1, name, VFS_MAX_PATH - 2);
        fspath[VFS_MAX_PATH - 1] = '\0';
    } else {
        if (parent_id < 0 || parent_id >= FAT32_MAX_NODES ||
            !priv->nodes[parent_id].used) return VFS_ERR_NOT_FOUND;

        const char* pp = priv->nodes[parent_id].path;
        int plen = strlen(pp);
        if (plen + 1 + (int)strlen(name) >= VFS_MAX_PATH) return VFS_ERR_TOO_BIG;

        strncpy(fspath, pp, VFS_MAX_PATH - 1);
        if (fspath[plen - 1] != '/') { fspath[plen] = '/'; fspath[plen+1] = '\0'; }
        strncat(fspath, name, VFS_MAX_PATH - strlen(fspath) - 1);
    }

    if (type == VFS_ENT_DIR) {
        int ret = fat32_mkdir(&priv->fs, fspath);
        return (ret == 0) ? VFS_OK : VFS_ERR_IO;
    } else {
        uint8_t attr = FAT32_ATTR_ARCHIVE;
        int ret = fat32_create(&priv->fs, fspath, attr);
        return (ret == 0) ? VFS_OK : VFS_ERR_IO;
    }
}

static VFS_Error fat32_remove_impl(VFS_FSInstance* inst, int id) {
    Fat32Private* priv = (Fat32Private*)inst->priv;
    if (!priv || !priv->mounted) return VFS_ERR_NO_FS;

    if (id < 0 || id >= FAT32_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    int ret = fat32_delete(&priv->fs, priv->nodes[id].path);
    if (ret == 0) fat32_free_node(priv, id);
    return (ret == 0) ? VFS_OK : VFS_ERR_IO;
}

static const VFS_FSDriver fat32_driver = {
    "fat32",
    fat32_mount_impl,
    fat32_unmount_impl,
    fat32_readdir_impl,
    fat32_lookup_impl,
    fat32_parent_impl,
    fat32_name_impl,
    fat32_type_impl,
    fat32_size_impl,
    fat32_create_impl,
    fat32_remove_impl,
    fat32_read_impl,
    fat32_write_impl
};

extern "C" VFS_Error vfs_fat32_register(void) {
    VFS_Error err = vfs_register_driver(&fat32_driver);
    return err;
}
