#include "vfs_fat16.h"
#include "fat16.h"

#include <lib/rmem/memory.h>
#include <lib/strtools/string.h>
#include <io/printg.h>

#define FAT16_MAX_NODES 128

struct Fat16Node {
    bool     used;
    bool     is_dir;
    char     path[VFS_MAX_PATH];
    uint32_t size;
};

struct Fat16Private {
    DFS_Fat16* fs;
    IDiskIO*   disk;
    uint32_t   lba_start;
    bool       mounted;

    Fat16Node nodes[FAT16_MAX_NODES];
};

static int fat16_alloc_node(Fat16Private* priv) {
    for (int i = 0; i < FAT16_MAX_NODES; i++) {
        if (!priv->nodes[i].used) return i;
    }
    return -1;
}

static void fat16_free_node(Fat16Private* priv, int id) {
    if (id >= 0 && id < FAT16_MAX_NODES)
        priv->nodes[id].used = false;
}

static void fat16_build_path(char* out, int out_size, const char* parent_path, const char* name) {
    int plen = strlen(parent_path);
    if (plen == 0 || parent_path[plen - 1] == '/') {
        snprintf(out, out_size, "%s%s", parent_path, name);
    } else {
        snprintf(out, out_size, "%s/%s", parent_path, name);
    }
}

static const char* fat16_basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

static int fat16_node_for_path(Fat16Private* priv, const char* fspath, int inode_id, bool is_dir, uint32_t size) {
    for (int i = 0; i < FAT16_MAX_NODES; i++) {
        if (priv->nodes[i].used && strcmp(priv->nodes[i].path, fspath) == 0)
            return i;
    }

    int id = fat16_alloc_node(priv);
    if (id < 0) return -1;

    Fat16Node& n = priv->nodes[id];
    n.used   = true;
    n.is_dir = is_dir;
    n.size   = size;
    strncpy(n.path, fspath, VFS_MAX_PATH - 1);
    n.path[VFS_MAX_PATH - 1] = '\0';

    return id;
}

static int fat16_resolve_inode(Fat16Private* priv, const char* fspath) {
    if (fspath[0] == '/' && fspath[1] == '\0') {
        return fat16_node_for_path(priv, "/", -1, true, 0);
    }

    char path_copy[VFS_MAX_PATH];
    strncpy(path_copy, fspath, VFS_MAX_PATH - 1);
    path_copy[VFS_MAX_PATH - 1] = '\0';

    int parent_inode = -1;
    char current_path[VFS_MAX_PATH] = "";

    char* token = strtok(path_copy, "/");
    while (token != nullptr) {
        if (current_path[0] == '\0') {
            strncpy(current_path, token, VFS_MAX_PATH - 1);
        } else {
            int len = strlen(current_path);
            if (len < VFS_MAX_PATH - 2) {
                current_path[len] = '/';
                current_path[len + 1] = '\0';
                strncat(current_path, token, VFS_MAX_PATH - len - 2);
            }
        }

        int inode = priv->fs->find(token, parent_inode);
        if (inode < 0) return -1;

        bool is_dir = (priv->fs->get_type(inode) == ENT_DIR);
        uint32_t sz = priv->fs->get_size(inode);

        int node_id = fat16_node_for_path(priv, current_path, inode, is_dir, sz);
        if (node_id < 0) return -1;

        parent_inode = inode;
        token = strtok(nullptr, "/");
    }

    return fat16_node_for_path(priv, fspath, parent_inode, priv->fs->get_type(parent_inode) == ENT_DIR, priv->fs->get_size(parent_inode));
}

static VFS_Error fat16_mount_impl(VFS_FSInstance* inst, void* addr, size_t size, IDiskIO* disk, uint32_t lba_start) {
    (void)addr; (void)size;

    if (!disk) {
        log(1, "FAT16: no disk provided");
        return VFS_ERR_BAD_ARG;
    }

    extern MemoryManager* kernel_mem_manager;

    Fat16Private* priv = (Fat16Private*)kernel_mem_manager->allocate(sizeof(Fat16Private));
    if (!priv) return VFS_ERR_NO_SPACE;

    memset(priv, 0, sizeof(Fat16Private));
    priv->disk      = disk;
    priv->lba_start = lba_start;
    priv->mounted   = false;

    priv->fs = (DFS_Fat16*)kernel_mem_manager->allocate(sizeof(DFS_Fat16));
    if (!priv->fs) {
        kernel_mem_manager->free(priv);
        return VFS_ERR_NO_SPACE;
    }

    new (priv->fs) DFS_Fat16(disk, lba_start);

    FS_Error err = priv->fs->mount();
    if (err != FS_OK) {
        log(1, "FAT16: mount failed at LBA %u: %d", lba_start, err);
        priv->fs->~DFS_Fat16();
        kernel_mem_manager->free(priv->fs);
        kernel_mem_manager->free(priv);
        return VFS_ERR_IO;
    }

    priv->mounted = true;
    inst->priv = priv;

    fat16_node_for_path(priv, "/", -1, true, 0);

    log(3, "FAT16: mounted at LBA %u", lba_start);
    return VFS_OK;
}

static VFS_Error fat16_unmount_impl(VFS_FSInstance* inst) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (priv) {
        if (priv->mounted && priv->fs) {
            priv->fs->unmount();
            priv->fs->~DFS_Fat16();
            extern MemoryManager* kernel_mem_manager;
            kernel_mem_manager->free(priv->fs);
        }
        extern MemoryManager* kernel_mem_manager;
        kernel_mem_manager->free(priv);
        inst->priv = nullptr;
    }
    return VFS_OK;
}

static int fat16_lookup_impl(VFS_FSInstance* inst, const char* name, int parent_id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted || !name) return -1;

    const char* parent_path = "/";
    if (parent_id >= 0 && parent_id < FAT16_MAX_NODES && priv->nodes[parent_id].used) {
        parent_path = priv->nodes[parent_id].path;
    }

    char fspath[VFS_MAX_PATH];
    fat16_build_path(fspath, sizeof(fspath), parent_path, name);

    int dfs_parent = -1;
    if (parent_id >= 0 && parent_id < FAT16_MAX_NODES && priv->nodes[parent_id].used) {
        char pp[VFS_MAX_PATH];
        strncpy(pp, parent_path, VFS_MAX_PATH - 1);
        pp[VFS_MAX_PATH - 1] = '\0';

        char* token = strtok(pp, "/");
        dfs_parent = -1;
        while (token != nullptr) {
            dfs_parent = priv->fs->find(token, dfs_parent);
            if (dfs_parent < 0) return -1;
            token = strtok(nullptr, "/");
        }
    }

    int dfs_inode = priv->fs->find(name, dfs_parent);
    if (dfs_inode < 0) return -1;

    bool is_dir = (priv->fs->get_type(dfs_inode) == ENT_DIR);
    uint32_t sz = priv->fs->get_size(dfs_inode);

    return fat16_node_for_path(priv, fspath, dfs_inode, is_dir, sz);
}

static VFS_Dir fat16_readdir_impl(VFS_FSInstance* inst, int dir_id) {
    VFS_Dir d;
    d.count = 0;
    d.error = VFS_OK;

    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted) {
        d.error = VFS_ERR_NO_FS;
        return d;
    }

    int dfs_parent = -1;
    if (dir_id >= 0 && dir_id < FAT16_MAX_NODES && priv->nodes[dir_id].used) {
        char path_copy[VFS_MAX_PATH];
        strncpy(path_copy, priv->nodes[dir_id].path, VFS_MAX_PATH - 1);
        path_copy[VFS_MAX_PATH - 1] = '\0';

        char* token = strtok(path_copy, "/");
        while (token != nullptr) {
            dfs_parent = priv->fs->find(token, dfs_parent);
            if (dfs_parent < 0) {
                d.error = VFS_ERR_NOT_DIR;
                return d;
            }
            token = strtok(nullptr, "/");
        }
    }

    FS_ListResult lr = priv->fs->list(dfs_parent);
    if (lr.error != FS_OK) {
        d.error = VFS_ERR_IO;
        return d;
    }

    const char* dir_path = (dir_id == -1) ? "/" : priv->nodes[dir_id].path;

    for (int i = 0; i < lr.count && d.count < VFS_MAX_DIR_ENTRIES; i++) {
        if (lr.entries[i].name[0] == '.' &&
            (lr.entries[i].name[1] == '\0' ||
             (lr.entries[i].name[1] == '.' && lr.entries[i].name[2] == '\0'))) {
            continue;
        }

        char child_path[VFS_MAX_PATH];
        fat16_build_path(child_path, sizeof(child_path), dir_path, lr.entries[i].name);

        bool is_dir = (lr.entries[i].type == ENT_DIR);
        int node_id = fat16_node_for_path(priv, child_path, -1, is_dir, lr.entries[i].size);

        strncpy(d.entries[d.count].name, lr.entries[i].name, VFS_MAX_NAME - 1);
        d.entries[d.count].name[VFS_MAX_NAME - 1] = '\0';
        d.entries[d.count].size = lr.entries[i].size;
        d.entries[d.count].type = is_dir ? VFS_ENT_DIR : VFS_ENT_FILE;
        d.count++;
    }

    return d;
}

static uint8_t fat16_type_impl(VFS_FSInstance* inst, int id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return VFS_ENT_NONE;
    return priv->nodes[id].is_dir ? VFS_ENT_DIR : VFS_ENT_FILE;
}

static uint32_t fat16_size_impl(VFS_FSInstance* inst, int id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return 0;

    if (id >= 0 && id < FAT16_MAX_NODES && priv->nodes[id].used && !priv->nodes[id].is_dir) {
        char path_copy[VFS_MAX_PATH];
        strncpy(path_copy, priv->nodes[id].path, VFS_MAX_PATH - 1);
        path_copy[VFS_MAX_PATH - 1] = '\0';

        int dfs_inode = -1;
        char* token = strtok(path_copy, "/");
        while (token != nullptr) {
            dfs_inode = priv->fs->find(token, dfs_inode);
            if (dfs_inode < 0) break;
            token = strtok(nullptr, "/");
        }
        if (dfs_inode >= 0) {
            priv->nodes[id].size = priv->fs->get_size(dfs_inode);
        }
    }

    return priv->nodes[id].size;
}

static const char* fat16_name_impl(VFS_FSInstance* inst, int id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return "";
    return fat16_basename(priv->nodes[id].path);
}

static int fat16_parent_impl(VFS_FSInstance* inst, int id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return -1;

    const char* path = priv->nodes[id].path;

    if (strcmp(path, "/") == 0) return -1;

    char parent_path[VFS_MAX_PATH];
    strncpy(parent_path, path, VFS_MAX_PATH - 1);
    parent_path[VFS_MAX_PATH - 1] = '\0';

    int len = strlen(parent_path);
    int slash = len - 1;
    while (slash > 0 && parent_path[slash] != '/') slash--;

    if (slash == 0) {
        return fat16_node_for_path(priv, "/", -1, true, 0);
    }

    parent_path[slash] = '\0';

    return fat16_resolve_inode(priv, parent_path);
}

static VFS_Error fat16_read_impl(VFS_FSInstance* inst, int id, void* buf, uint32_t* inout_size) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted || !buf || !inout_size)
        return VFS_ERR_BAD_ARG;

    if (id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    Fat16Node& n = priv->nodes[id];
    if (n.is_dir) return VFS_ERR_NOT_FILE;

    char path_copy[VFS_MAX_PATH];
    strncpy(path_copy, n.path, VFS_MAX_PATH - 1);
    path_copy[VFS_MAX_PATH - 1] = '\0';

    int dfs_inode = -1;
    char* token = strtok(path_copy, "/");
    while (token != nullptr) {
        dfs_inode = priv->fs->find(token, dfs_inode);
        if (dfs_inode < 0) return VFS_ERR_NOT_FOUND;
        token = strtok(nullptr, "/");
    }

    uint32_t to_read = *inout_size;
    FS_Error err = priv->fs->read(dfs_inode, buf, &to_read);
    if (err != FS_OK) return VFS_ERR_IO;

    *inout_size = to_read;
    return VFS_OK;
}

static VFS_Error fat16_write_impl(VFS_FSInstance* inst, int id, const void* buf, uint32_t size) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted || !buf)
        return VFS_ERR_BAD_ARG;

    if (id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    Fat16Node& n = priv->nodes[id];
    if (n.is_dir) return VFS_ERR_NOT_FILE;

    char path_copy[VFS_MAX_PATH];
    strncpy(path_copy, n.path, VFS_MAX_PATH - 1);
    path_copy[VFS_MAX_PATH - 1] = '\0';

    int dfs_inode = -1;
    char* token = strtok(path_copy, "/");
    while (token != nullptr) {
        dfs_inode = priv->fs->find(token, dfs_inode);
        if (dfs_inode < 0) return VFS_ERR_NOT_FOUND;
        token = strtok(nullptr, "/");
    }

    FS_Error err = priv->fs->write(dfs_inode, buf, size);
    if (err != FS_OK) return VFS_ERR_IO;

    n.size = priv->fs->get_size(dfs_inode);
    return VFS_OK;
}

static VFS_Error fat16_create_impl(VFS_FSInstance* inst, const char* name, uint8_t type, int parent_id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted) return VFS_ERR_NO_FS;
    if (!name || !*name) return VFS_ERR_BAD_ARG;

    int dfs_parent = -1;
    const char* parent_path = "/";

    if (parent_id >= 0 && parent_id < FAT16_MAX_NODES && priv->nodes[parent_id].used) {
        parent_path = priv->nodes[parent_id].path;

        char path_copy[VFS_MAX_PATH];
        strncpy(path_copy, parent_path, VFS_MAX_PATH - 1);
        path_copy[VFS_MAX_PATH - 1] = '\0';

        char* token = strtok(path_copy, "/");
        while (token != nullptr) {
            dfs_parent = priv->fs->find(token, dfs_parent);
            if (dfs_parent < 0) return VFS_ERR_NOT_FOUND;
            token = strtok(nullptr, "/");
        }
    }

    if (priv->fs->find(name, dfs_parent) >= 0)
        return VFS_ERR_EXISTS;

    uint8_t dfs_type = (type == VFS_ENT_DIR) ? ENT_DIR : ENT_FILE;
    FS_Error err = priv->fs->create(name, dfs_type, dfs_parent);
    if (err != FS_OK) {
        if (err == FS_ERR_EXISTS) return VFS_ERR_EXISTS;
        if (err == FS_ERR_NO_SPACE) return VFS_ERR_NO_SPACE;
        return VFS_ERR_IO;
    }

    char fspath[VFS_MAX_PATH];
    fat16_build_path(fspath, sizeof(fspath), parent_path, name);

    int new_dfs_inode = priv->fs->find(name, dfs_parent);
    uint32_t sz = (new_dfs_inode >= 0) ? priv->fs->get_size(new_dfs_inode) : 0;
    bool is_dir = (type == VFS_ENT_DIR);

    fat16_node_for_path(priv, fspath, new_dfs_inode, is_dir, sz);

    return VFS_OK;
}

static VFS_Error fat16_remove_impl(VFS_FSInstance* inst, int id) {
    Fat16Private* priv = (Fat16Private*)inst->priv;
    if (!priv || !priv->mounted) return VFS_ERR_NO_FS;

    if (id < 0 || id >= FAT16_MAX_NODES || !priv->nodes[id].used)
        return VFS_ERR_NOT_FOUND;

    char path_copy[VFS_MAX_PATH];
    strncpy(path_copy, priv->nodes[id].path, VFS_MAX_PATH - 1);
    path_copy[VFS_MAX_PATH - 1] = '\0';

    int dfs_inode = -1;
    char* token = strtok(path_copy, "/");
    while (token != nullptr) {
        dfs_inode = priv->fs->find(token, dfs_inode);
        if (dfs_inode < 0) return VFS_ERR_NOT_FOUND;
        token = strtok(nullptr, "/");
    }

    FS_Error err = priv->fs->remove(dfs_inode);
    if (err != FS_OK) {
        if (err == FS_ERR_NOT_FOUND) return VFS_ERR_NOT_FOUND;
        return VFS_ERR_IO;
    }

    fat16_free_node(priv, id);
    return VFS_OK;
}

static const VFS_FSDriver fat16_driver = {
    "fat16",
    fat16_mount_impl,
    fat16_unmount_impl,
    fat16_readdir_impl,
    fat16_lookup_impl,
    fat16_parent_impl,
    fat16_name_impl,
    fat16_type_impl,
    fat16_size_impl,
    fat16_create_impl,
    fat16_remove_impl,
    fat16_read_impl,
    fat16_write_impl
};

extern "C" VFS_Error vfs_fat16_register(void) {
    return vfs_register_driver(&fat16_driver);
}
