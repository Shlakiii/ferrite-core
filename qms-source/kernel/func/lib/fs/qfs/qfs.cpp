#define QFS_BLOCK_SIZE      256
#define QFS_MAX_INODES      64
#define QFS_MAX_NAME_LEN    32
#define QFS_DIRECT_BLOCKS   6
#define QFS_MAX_BLOCKS      512
#define QFS_MAGIC           0x51465300u
#define QFS_VERSION         1

#include "qfs.h"

#include <lib/strtools/string.h>

#include <io/printg.h>

#define QFS_BITMAP_BYTES  ((QFS_MAX_BLOCKS + 7) / 8)
#define QFS_NO_BLOCK      0xFFFFu
#define QFS_NO_INODE      0xFFu

struct __attribute__((packed)) QfsHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  block_size_log2;
    uint16_t inode_count;
    uint16_t block_count;
    uint16_t root_inode;
    uint8_t  block_bitmap[QFS_BITMAP_BYTES];
    uint8_t  inode_bitmap[QFS_MAX_INODES];
};

struct __attribute__((packed)) QfsInode {
    uint8_t  used;
    uint8_t  type;
    uint8_t  parent;
    uint8_t  _res;
    uint32_t size;
    char     name[QFS_MAX_NAME_LEN];
    uint16_t blocks[QFS_DIRECT_BLOCKS];
    uint16_t indirect;
};

struct QfsCtx {
    QfsHeader* hdr;
    QfsInode*  inodes;
    uint8_t*   data_start;
    uint16_t   block_count;
    uint16_t   inode_count;
};

static_assert(sizeof(QfsCtx) <= 1024, "QfsCtx too large for inst_storage[1024] increase it in vfs.cpp");

static inline uint8_t* qfs_block_ptr(QfsCtx* ctx, uint16_t idx) {
    return ctx->data_start + (uint32_t)idx * QFS_BLOCK_SIZE;
}

static uint16_t qfs_alloc_block(QfsCtx* ctx) {
    for (int i = 0; i < ctx->block_count; i++) {
        int byte = i >> 3, bit = i & 7;
        if (!(ctx->hdr->block_bitmap[byte] & (1u << bit))) {
            ctx->hdr->block_bitmap[byte] |= (uint8_t)(1u << bit);
            memset(qfs_block_ptr(ctx, (uint16_t)i), 0, QFS_BLOCK_SIZE);
            return (uint16_t)i;
        }
    }
    return QFS_NO_BLOCK;
}

static void qfs_free_block(QfsCtx* ctx, uint16_t idx) {
    if (idx == QFS_NO_BLOCK || idx >= ctx->block_count) return;
    ctx->hdr->block_bitmap[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}

static uint8_t qfs_alloc_inode(QfsCtx* ctx) {
    for (int i = 0; i < ctx->inode_count; i++) {
        if (!ctx->hdr->inode_bitmap[i]) {
            ctx->hdr->inode_bitmap[i] = 1;
            QfsInode* in = &ctx->inodes[i];
            memset(in, 0, sizeof(QfsInode));
            in->used     = 1;
            in->parent   = QFS_NO_INODE;
            in->indirect = QFS_NO_BLOCK;
            for (int j = 0; j < QFS_DIRECT_BLOCKS; j++)
                in->blocks[j] = QFS_NO_BLOCK;
            return (uint8_t)i;
        }
    }
    return QFS_NO_INODE;
}

static void qfs_free_inode(QfsCtx* ctx, uint8_t idx) {
    if (idx >= ctx->inode_count) return;
    QfsInode* in = &ctx->inodes[idx];
    for (int i = 0; i < QFS_DIRECT_BLOCKS; i++) {
        if (in->blocks[i] != QFS_NO_BLOCK) {
            qfs_free_block(ctx, in->blocks[i]);
            in->blocks[i] = QFS_NO_BLOCK;
        }
    }
    if (in->indirect != QFS_NO_BLOCK) {
        uint16_t* ind = (uint16_t*)qfs_block_ptr(ctx, in->indirect);
        int entries   = QFS_BLOCK_SIZE / 2;
        for (int i = 0; i < entries; i++) {
            if (ind[i] != QFS_NO_BLOCK) { qfs_free_block(ctx, ind[i]); ind[i] = QFS_NO_BLOCK; }
        }
        qfs_free_block(ctx, in->indirect);
        in->indirect = QFS_NO_BLOCK;
    }
    in->used = 0;
    ctx->hdr->inode_bitmap[idx] = 0;
}

static uint16_t qfs_get_block(QfsCtx* ctx, QfsInode* in, uint32_t bn, bool alloc) {
    if (bn < (uint32_t)QFS_DIRECT_BLOCKS) {
        if (in->blocks[bn] == QFS_NO_BLOCK && alloc) {
            uint16_t b = qfs_alloc_block(ctx);
            if (b == QFS_NO_BLOCK) return QFS_NO_BLOCK;
            in->blocks[bn] = b;
        }
        return in->blocks[bn];
    }
    uint32_t ind_num     = bn - QFS_DIRECT_BLOCKS;
    uint32_t ind_entries = (uint32_t)(QFS_BLOCK_SIZE / 2);
    if (ind_num >= ind_entries) return QFS_NO_BLOCK;
    if (in->indirect == QFS_NO_BLOCK) {
        if (!alloc) return QFS_NO_BLOCK;
        uint16_t ib = qfs_alloc_block(ctx);
        if (ib == QFS_NO_BLOCK) return QFS_NO_BLOCK;
        in->indirect = ib;
        uint16_t* p  = (uint16_t*)qfs_block_ptr(ctx, ib);
        for (uint32_t i = 0; i < ind_entries; i++) p[i] = QFS_NO_BLOCK;
    }
    uint16_t* ind = (uint16_t*)qfs_block_ptr(ctx, in->indirect);
    if (ind[ind_num] == QFS_NO_BLOCK && alloc) {
        uint16_t b = qfs_alloc_block(ctx);
        if (b == QFS_NO_BLOCK) return QFS_NO_BLOCK;
        ind[ind_num] = b;
    }
    return ind[ind_num];
}

static uint8_t log2_u32(uint32_t v) {
    uint8_t n = 0; while (v > 1) { v >>= 1; n++; } return n;
}

static inline uint32_t qfs_meta_size(void) {
    return (uint32_t)sizeof(QfsHeader)
         + (uint32_t)sizeof(QfsInode) * QFS_MAX_INODES;
}

static bool qfs_format(QfsCtx* ctx, void* addr, size_t size) {
    uint32_t meta = qfs_meta_size();

    if ((uint32_t)size < meta + QFS_BLOCK_SIZE) {
        return false;
    }

    uint32_t n_blocks = ((uint32_t)size - meta) / QFS_BLOCK_SIZE;
    if (n_blocks > QFS_MAX_BLOCKS) n_blocks = QFS_MAX_BLOCKS;

    memset(addr, 0, size);

    ctx->hdr        = (QfsHeader*)addr;
    ctx->inodes     = (QfsInode*)((uint8_t*)addr + sizeof(QfsHeader));
    ctx->data_start = (uint8_t*)addr + meta;
    ctx->block_count= (uint16_t)n_blocks;
    ctx->inode_count= (uint16_t)QFS_MAX_INODES;

    QfsHeader* h       = ctx->hdr;
    h->magic           = QFS_MAGIC;
    h->version         = QFS_VERSION;
    h->block_size_log2 = log2_u32(QFS_BLOCK_SIZE);
    h->inode_count     = (uint16_t)QFS_MAX_INODES;
    h->block_count     = (uint16_t)n_blocks;
    h->root_inode      = 0;

    uint8_t root = qfs_alloc_inode(ctx);
    if (root == QFS_NO_INODE) {
        log(1, "QFS fmt: root inode alloc failed");
        return false;
    }
    QfsInode* ri = &ctx->inodes[root];
    ri->type    = VFS_ENT_DIR;
    ri->parent  = QFS_NO_INODE;
    ri->name[0] = '/'; ri->name[1] = '\0';

    log(3, "QFS: ready %u KB", (unsigned)(n_blocks * QFS_BLOCK_SIZE / 1024));
    return true;
}

static bool qfs_attach(QfsCtx* ctx, void* addr, size_t size) {
    if (!addr || size < sizeof(QfsHeader)) return false;
    QfsHeader* h = (QfsHeader*)addr;
    if (h->magic != QFS_MAGIC || h->version != QFS_VERSION) return false;
    uint32_t meta = (uint32_t)sizeof(QfsHeader)
                  + (uint32_t)sizeof(QfsInode) * h->inode_count;
    if ((uint32_t)size < meta) return false;
    ctx->hdr        = h;
    ctx->inodes     = (QfsInode*)((uint8_t*)addr + sizeof(QfsHeader));
    ctx->data_start = (uint8_t*)addr + meta;
    ctx->block_count= h->block_count;
    ctx->inode_count= h->inode_count;
    log(3, "QFS: attached existing volume (%u blocks)", (unsigned)h->block_count);
    return true;
}

static VFS_Error qfs_mount_impl(VFS_FSInstance* inst, void* addr, size_t size, IDiskIO* disk, uint32_t lba) {
    (void)disk; (void)lba;
    if (!addr || size == 0) { log(1, "QFS mount: bad addr/size"); return VFS_ERR_BAD_ARG; }
    if (!inst->priv)        { log(1, "QFS mount: priv=NULL");     return VFS_ERR_BAD_ARG; }

    QfsCtx* ctx = (QfsCtx*)inst->priv;
    memset(ctx, 0, sizeof(QfsCtx));

    if (!qfs_attach(ctx, addr, size))
        if (!qfs_format(ctx, addr, size))
            return VFS_ERR_NO_SPACE;
    return VFS_OK;
}

static VFS_Error qfs_unmount_impl(VFS_FSInstance* inst) { (void)inst; return VFS_OK; }

static VFS_Dir qfs_readdir_impl(VFS_FSInstance* inst, int dir_id) {
    VFS_Dir d; d.count = 0; d.error = VFS_OK;
    QfsCtx* ctx    = (QfsCtx*)inst->priv;
    uint8_t target = (dir_id < 0) ? 0u : (uint8_t)dir_id;
    for (int i = 0; i < ctx->inode_count && d.count < VFS_MAX_DIR_ENTRIES; i++) {
        QfsInode* in = &ctx->inodes[i];
        if (!in->used || (uint8_t)i == target || in->parent != target) continue;
        VFS_DirEntry* e = &d.entries[d.count++];
        strncpy(e->name, in->name, VFS_MAX_NAME - 1);
        e->name[VFS_MAX_NAME - 1] = '\0';
        e->size = in->size; e->type = in->type;
    }
    return d;
}

static int qfs_lookup_impl(VFS_FSInstance* inst, const char* name, int parent_id) {
    QfsCtx* ctx    = (QfsCtx*)inst->priv;
    uint8_t target = (parent_id < 0) ? 0u : (uint8_t)parent_id;
    for (int i = 0; i < ctx->inode_count; i++) {
        QfsInode* in = &ctx->inodes[i];
        if (!in->used || in->parent != target) continue;
        if (strcmp(in->name, name) == 0) return i;
    }
    return -1;
}

static int          qfs_parent_impl(VFS_FSInstance* inst, int id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count) return -1;
    QfsInode* in = &ctx->inodes[id];
    if (!in->used || in->parent == QFS_NO_INODE) return -1;
    return (int)in->parent;
}

static const char*  qfs_name_impl(VFS_FSInstance* inst, int id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count) return "";
    return ctx->inodes[id].name;
}

static uint8_t      qfs_type_impl(VFS_FSInstance* inst, int id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count) return VFS_ENT_NONE;
    return ctx->inodes[id].type;
}

static uint32_t     qfs_size_impl(VFS_FSInstance* inst, int id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count) return 0;
    return ctx->inodes[id].size;
}

static VFS_Error qfs_create_impl(VFS_FSInstance* inst, const char* name, uint8_t type, int parent_id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (!name || !name[0]) return VFS_ERR_BAD_ARG;
    if (type != VFS_ENT_FILE && type != VFS_ENT_DIR) return VFS_ERR_BAD_ARG;
    int nlen = 0; while (name[nlen]) nlen++;
    if (nlen >= QFS_MAX_NAME_LEN) return VFS_ERR_TOO_BIG;
    if (qfs_lookup_impl(inst, name, parent_id) >= 0) return VFS_ERR_EXISTS;
    uint8_t idx = qfs_alloc_inode(ctx);
    if (idx == QFS_NO_INODE) return VFS_ERR_NO_SPACE;
    QfsInode* in = &ctx->inodes[idx];
    in->type   = type;
    in->parent = (parent_id < 0) ? 0u : (uint8_t)parent_id;
    in->size   = 0;
    strncpy(in->name, name, QFS_MAX_NAME_LEN - 1);
    in->name[QFS_MAX_NAME_LEN - 1] = '\0';
    return VFS_OK;
}

static VFS_Error qfs_remove_impl(VFS_FSInstance* inst, int id) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count) return VFS_ERR_BAD_ARG;
    if (!ctx->inodes[id].used) return VFS_ERR_NOT_FOUND;
    if (id == 0) return VFS_ERR_PERM;
    qfs_free_inode(ctx, (uint8_t)id);
    return VFS_OK;
}

static VFS_Error qfs_read_impl(VFS_FSInstance* inst, int id, void* buf, uint32_t* inout_size) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count || !buf || !inout_size) return VFS_ERR_BAD_ARG;
    QfsInode* in = &ctx->inodes[id];
    if (!in->used) return VFS_ERR_NOT_FOUND;
    if (in->type != VFS_ENT_FILE) return VFS_ERR_NOT_FILE;
    uint32_t to_read = *inout_size;
    if (to_read > in->size) to_read = in->size;
    uint8_t* dst = (uint8_t*)buf; uint32_t done = 0;
    while (done < to_read) {
        uint32_t bn = done / QFS_BLOCK_SIZE, off = done % QFS_BLOCK_SIZE;
        uint32_t chunk = QFS_BLOCK_SIZE - off;
        if (chunk > to_read - done) chunk = to_read - done;
        uint16_t blk = qfs_get_block(ctx, in, bn, false);
        if (blk == QFS_NO_BLOCK) break;
        memcpy(dst + done, qfs_block_ptr(ctx, blk) + off, chunk);
        done += chunk;
    }
    *inout_size = done; return VFS_OK;
}

static VFS_Error qfs_write_impl(VFS_FSInstance* inst, int id, const void* buf, uint32_t size) {
    QfsCtx* ctx = (QfsCtx*)inst->priv;
    if (id < 0 || id >= ctx->inode_count || !buf) return VFS_ERR_BAD_ARG;
    QfsInode* in = &ctx->inodes[id];
    if (!in->used) return VFS_ERR_NOT_FOUND;
    if (in->type != VFS_ENT_FILE) return VFS_ERR_NOT_FILE;
    uint32_t max_size = ((uint32_t)QFS_DIRECT_BLOCKS + QFS_BLOCK_SIZE / 2u) * QFS_BLOCK_SIZE;
    if (size > max_size) return VFS_ERR_TOO_BIG;
    const uint8_t* src = (const uint8_t*)buf; uint32_t done = 0;
    while (done < size) {
        uint32_t bn = done / QFS_BLOCK_SIZE, off = done % QFS_BLOCK_SIZE;
        uint32_t chunk = QFS_BLOCK_SIZE - off;
        if (chunk > size - done) chunk = size - done;
        uint16_t blk = qfs_get_block(ctx, in, bn, true);
        if (blk == QFS_NO_BLOCK) return VFS_ERR_NO_SPACE;
        memcpy(qfs_block_ptr(ctx, blk) + off, src + done, chunk);
        done += chunk;
    }
    in->size = size; return VFS_OK;
}

static const VFS_FSDriver qfs_driver = {
    "qfs",
    qfs_mount_impl,  qfs_unmount_impl,
    qfs_readdir_impl, qfs_lookup_impl, qfs_parent_impl,
    qfs_name_impl,   qfs_type_impl,   qfs_size_impl,
    qfs_create_impl, qfs_remove_impl,
    qfs_read_impl,   qfs_write_impl,
};

extern "C" VFS_Error vfs_qfs_register(void) {
    VFS_Error err = vfs_register_driver(&qfs_driver);

    switch (err) {
        case VFS_OK:
            log(3, "QFS: registered");
            break;
        case VFS_ERR_EXISTS:
            log(3, "QFS: already registered");
            break;
        case VFS_ERR_NO_SPACE:
            log(1, "QFS: driver table full! "
                   "Increase VFS_MAX_FS_DRIVERS in vfs.h (now=%d)",
                   VFS_MAX_FS_DRIVERS);
            break;
        default:
            log(1, "QFS: register error %d: %s", (int)err, vfs_strerror(err));
            break;
    }
    return err;
}
