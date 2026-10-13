#include "vfs.h"

#include <lib/strtools/string.h>

#include <io/printg.h>

struct MountPoint {
    bool active;
    char path[VFS_MAX_PATH];
    bool bind;

    VFS_FSInstance inst;
    char           inst_storage[1024];

    int  bind_mount_idx;
    int  bind_dir_id;
};

struct FileDesc {
    bool   open;
    int    mount_idx;
    int    node_id;
    int    flags;
    uint32_t pos;
};

static MountPoint g_mounts[VFS_MAX_MOUNTS];
static FileDesc   g_fds   [VFS_MAX_FD];

static const VFS_FSDriver* g_drivers[VFS_MAX_FS_DRIVERS];
static int g_driver_count = 0;

static int  g_cwd_mount = -1;
static int  g_cwd_id    = -1;
static char g_cwd_abs[VFS_MAX_PATH] = { '/', '\0' };

static int path_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

static void path_normalize(const char* src, char* dst, int max) {
    static const int PARTS = 32;
    const char* parts[PARTS];
    int         lens [PARTS];
    int         nparts = 0;
    bool        absolute = (src[0] == '/');

    const char* p = src;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* seg = p;
        int len = 0;
        while (*p && *p != '/') { p++; len++; }
        if (len == 1 && seg[0] == '.') continue;
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (nparts > 0) nparts--;
            continue;
        }
        if (nparts < PARTS) {
            parts[nparts] = seg;
            lens [nparts] = len;
            nparts++;
        }
    }

    int pos = 0;
    if (absolute && pos < max - 1) dst[pos++] = '/';
    for (int i = 0; i < nparts; i++) {
        if (i > 0 && pos < max - 1) dst[pos++] = '/';
        for (int j = 0; j < lens[i] && pos < max - 1; j++)
            dst[pos++] = parts[i][j];
    }
    if (pos == 0) { dst[pos++] = '/'; }
    dst[pos] = '\0';
}

static void path_absolute(const char* rel, char* out, int max) {
    if (rel[0] == '/') {
        path_normalize(rel, out, max);
        return;
    }
    const char* cwd = vfs_getcwd();
    char tmp[VFS_MAX_PATH];
    int pos = 0;
    while (cwd[pos] && pos < VFS_MAX_PATH - 2) { tmp[pos] = cwd[pos]; pos++; }
    if (pos > 0 && tmp[pos - 1] != '/') tmp[pos++] = '/';
    const char* r = rel;
    while (*r && pos < VFS_MAX_PATH - 1) { tmp[pos++] = *r++; }
    tmp[pos] = '\0';
    path_normalize(tmp, out, max);
}

static bool path_starts_with(const char* path, const char* prefix) {
    int plen = 0;
    while (prefix[plen]) plen++;
    if (plen == 1 && prefix[0] == '/') return path[0] == '/';
    for (int i = 0; i < plen; i++) {
        if (path[i] != prefix[i]) return false;
    }
    return (path[plen] == '/' || path[plen] == '\0');
}

VFS_Error vfs_register_driver(const VFS_FSDriver* drv) {
    if (!drv || !drv->name)               return VFS_ERR_BAD_ARG;
    if (g_driver_count >= VFS_MAX_FS_DRIVERS) return VFS_ERR_NO_SPACE;
    for (int i = 0; i < g_driver_count; i++)
        if (strcmp(g_drivers[i]->name, drv->name) == 0) return VFS_ERR_EXISTS;
    g_drivers[g_driver_count++] = drv;
    log(3, "VFS: driver '%s' registered", drv->name);
    return VFS_OK;
}

const VFS_FSDriver* vfs_find_driver(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < g_driver_count; i++)
        if (strcmp(g_drivers[i]->name, name) == 0) return g_drivers[i];
    return nullptr;
}

int vfs_list_drivers(const VFS_FSDriver** out, int max) {
    int n = (g_driver_count < max) ? g_driver_count : max;
    for (int i = 0; i < n; i++) out[i] = g_drivers[i];
    return n;
}

static int find_mount_for_path(const char* abs_path) {
    int best = -1, best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) continue;
        if (path_starts_with(abs_path, g_mounts[i].path)) {
            int len = 0;
            while (g_mounts[i].path[len]) len++;
            if (len > best_len) { best = i; best_len = len; }
        }
    }
    return best;
}

static int find_free_mount() {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (!g_mounts[i].active) return i;
    return -1;
}

struct ResolveResult {
    int mount_idx;
    int node_id;
    bool found;
};

static ResolveResult vfs_resolve(const char* path);

static void resolve_bind(int& midx, int& node_id) {
    int guard = VFS_MAX_MOUNTS;
    while (guard-- > 0 && g_mounts[midx].active && g_mounts[midx].bind) {
        int bmi = g_mounts[midx].bind_mount_idx;
        int bid = g_mounts[midx].bind_dir_id;
        if (node_id == -1)
            node_id = bid;
        midx = bmi;
    }
}

static VFS_FSInstance* inst_for(int midx, int node_id) {
    (void)node_id;
    resolve_bind(midx, node_id);
    return &g_mounts[midx].inst;
}

static inline VFS_FSInstance* rr_inst(ResolveResult& r) {
    int m = r.mount_idx, n = r.node_id;
    resolve_bind(m, n);
    return &g_mounts[m].inst;
}

static ResolveResult vfs_resolve_in(int midx, int start_id, const char* rel_path) {
    ResolveResult r; r.found = true;

    int real_midx  = midx;
    int real_nodeid = start_id;
    resolve_bind(real_midx, real_nodeid);

    r.mount_idx = real_midx;
    r.node_id   = real_nodeid;

    VFS_FSInstance* inst = &g_mounts[real_midx].inst;
    if (!inst->driver) { r.found = false; return r; }

    const char* p = rel_path;
    while (*p == '/') p++;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char* seg = p;
        int seg_len = 0;
        while (*p && *p != '/') { p++; seg_len++; }
        if (seg_len == 0) continue;

        char seg_buf[VFS_MAX_NAME];
        if (seg_len >= VFS_MAX_NAME) { r.found = false; return r; }
        for (int i = 0; i < seg_len; i++) seg_buf[i] = seg[i];
        seg_buf[seg_len] = '\0';

        if (strcmp(seg_buf, ".") == 0) continue;
        if (strcmp(seg_buf, "..") == 0) {
            if (r.node_id != -1) {
                int par = inst->driver->fs_parent(inst, r.node_id);
                r.node_id = par;
            }
            continue;
        }

        int next = inst->driver->fs_lookup(inst, seg_buf, r.node_id);
        if (next == -1) { r.found = false; return r; }

        r.node_id = next;
    }
    return r;
}

static ResolveResult vfs_resolve(const char* path) {
    ResolveResult bad; bad.found = false; bad.mount_idx = -1; bad.node_id = -1;
    if (!path || !*path) return bad;

    char abs[VFS_MAX_PATH];
    path_absolute(path, abs, VFS_MAX_PATH);

    int midx = find_mount_for_path(abs);
    if (midx < 0) return bad;

    MountPoint& mp = g_mounts[midx];
    int mlen = 0; while (mp.path[mlen]) mlen++;
    const char* rel = abs + mlen;

    int start_id = -1;
    ResolveResult rr = vfs_resolve_in(midx, start_id, rel);

    return rr;
}

struct ParentResult {
    int mount_idx;
    int parent_id;
    char name[VFS_MAX_NAME];
    bool ok;
};

static ParentResult vfs_resolve_parent(const char* path) {
    ParentResult pr; pr.ok = false; pr.mount_idx = -1; pr.parent_id = -1;
    if (!path || !*path) return pr;

    char abs[VFS_MAX_PATH];
    path_absolute(path, abs, VFS_MAX_PATH);

    int len = 0; while (abs[len]) len++;
    int slash = len - 1;
    while (slash > 0 && abs[slash] != '/') slash--;

    const char* name_part = abs + slash + 1;
    int name_len = len - slash - 1;
    if (name_len == 0 || name_len >= VFS_MAX_NAME) return pr;
    for (int i = 0; i < name_len; i++) pr.name[i] = name_part[i];
    pr.name[name_len] = '\0';

    char parent_path[VFS_MAX_PATH];
    if (slash == 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        for (int i = 0; i < slash; i++) parent_path[i] = abs[i];
        parent_path[slash] = '\0';
    }

    ResolveResult r = vfs_resolve(parent_path);
    if (!r.found) return pr;

    pr.ok         = true;
    pr.mount_idx  = r.mount_idx;
    pr.parent_id  = r.node_id;
    return pr;
}

VFS_Error vfs_init(const char* fs_name, void* addr, size_t size, IDiskIO* disk, uint32_t lba_start) {
    log(3, "VFS: Initialized...");

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) g_mounts[i].active = false;
    for (int i = 0; i < VFS_MAX_FD;     i++) g_fds[i].open   = false;
    g_cwd_mount = -1;
    g_cwd_id    = -1;
    g_cwd_abs[0] = '/'; g_cwd_abs[1] = '\0';

    VFS_Error err = vfs_mount("/", fs_name, addr, size, disk, lba_start, nullptr);
    if (err != VFS_OK) return err;

    g_cwd_mount = find_mount_for_path("/");
    g_cwd_id    = -1;

    log(3, "VFS: Init done.");
    return VFS_OK;
}

extern "C" void vfs_force_init(void) {
    g_driver_count = 0;
    for (int i = 0; i < VFS_MAX_FS_DRIVERS; i++)
        g_drivers[i] = nullptr;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) g_mounts[i].active = false;
    for (int i = 0; i < VFS_MAX_FD; i++)     g_fds[i].open = false;

    g_cwd_mount = -1;
    g_cwd_id    = -1;
    g_cwd_abs[0] = '/'; g_cwd_abs[1] = '\0';
}

VFS_Error vfs_mount(const char* mount_point, const char* fs_name, void* addr, size_t size, IDiskIO* disk, uint32_t lba_start, const char* source_path) {
    if (!mount_point || !*mount_point) return VFS_ERR_BAD_ARG;

    char mp_norm[VFS_MAX_PATH];
    path_normalize(mount_point, mp_norm, VFS_MAX_PATH);

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].path, mp_norm) == 0)
            return VFS_ERR_BUSY;
    }

    int slot = find_free_mount();
    if (slot < 0) return VFS_ERR_NO_SPACE;

    MountPoint& mp = g_mounts[slot];
    mp.active = false;
    mp.bind   = false;
    path_copy(mp.path, mp_norm, VFS_MAX_PATH);

    if (!fs_name && source_path) {
        char src_abs[VFS_MAX_PATH];
        path_absolute(source_path, src_abs, VFS_MAX_PATH);

        ResolveResult r = vfs_resolve(src_abs);
        if (!r.found) return VFS_ERR_NOT_FOUND;

        if (r.node_id != -1) {
            VFS_FSInstance* sinst = rr_inst(r);
            uint8_t t = sinst->driver->fs_type(sinst, r.node_id);
            if (t != VFS_ENT_DIR) return VFS_ERR_NOT_DIR;
        }

        mp.bind           = true;
        mp.bind_mount_idx = r.mount_idx;
        mp.bind_dir_id    = r.node_id;
        mp.inst.driver    = nullptr;
        mp.inst.priv      = nullptr;
        mp.active         = true;

        return VFS_OK;
    }

    if (!fs_name) return VFS_ERR_BAD_ARG;

    const VFS_FSDriver* drv = vfs_find_driver(fs_name);
    if (!drv) {
        log(1, "VFS: driver '%s' not found", fs_name);
        return VFS_ERR_NOT_FOUND;
    }

    mp.inst.driver = drv;
    mp.inst.priv   = mp.inst_storage;

    VFS_Error err = drv->fs_mount(&mp.inst, addr, size, disk, lba_start);
    if (err != VFS_OK) {
        log(1, "VFS: mount '%s' at '%s' failed: %s", fs_name, mp_norm, vfs_strerror(err));
        mp.active = false;
        return err;
    }

    mp.active = true;
    log(3, "VFS: mounted '%s' at '%s'", fs_name, mp_norm);
    return VFS_OK;
}

VFS_Error vfs_umount(const char* mount_point) {
    if (!mount_point || !*mount_point) return VFS_ERR_BAD_ARG;

    char mp_norm[VFS_MAX_PATH];
    path_normalize(mount_point, mp_norm, VFS_MAX_PATH);

    if (mp_norm[0] == '/' && mp_norm[1] == '\0') {
        log(2, "VFS: cannot umount root");
        return VFS_ERR_BUSY;
    }

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) continue;
        if (strcmp(g_mounts[i].path, mp_norm) != 0) continue;

        for (int f = 0; f < VFS_MAX_FD; f++) {
            if (g_fds[f].open && g_fds[f].mount_idx == i) {
                log(2, "VFS: umount '%s': busy (open fd)", mp_norm);
                return VFS_ERR_BUSY;
            }
        }

        if (!g_mounts[i].bind && g_mounts[i].inst.driver) {
            g_mounts[i].inst.driver->fs_unmount(&g_mounts[i].inst);
        }
        g_mounts[i].active = false;
        log(3, "VFS: unmounted '%s'", mp_norm);
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

static int alloc_fd() {
    for (int i = 0; i < VFS_MAX_FD; i++)
        if (!g_fds[i].open) return i;
    return -1;
}

int vfs_open(const char* path, int flags) {
    if (!path || !*path) return -1;

    ResolveResult r = vfs_resolve(path);

    if (!r.found && (flags & VFS_O_CREAT)) {
        VFS_Error ce = vfs_creat(path, VFS_ENT_FILE);
        if (ce != VFS_OK) return -(int)ce;
        r = vfs_resolve(path);
    }
    if (!r.found) return -(int)VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    if (!inst || !inst->driver) {
        log(1, "VFS open '%s': NULL driver after resolve (mount_idx=%d node_id=%d)", path, r.mount_idx, r.node_id);
        return -(int)VFS_ERR_NO_FS;
    }

    if (r.node_id != -1) {
        uint8_t t = inst->driver->fs_type(inst, r.node_id);
        if (t != VFS_ENT_FILE) return -(int)VFS_ERR_NOT_FILE;
    }

    if ((flags & VFS_O_TRUNC) && r.node_id != -1) {
        inst->driver->fs_write(inst, r.node_id, "", 0);
    }

    int fd = alloc_fd();
    if (fd < 0) return -(int)VFS_ERR_NO_FD;

    FileDesc& f  = g_fds[fd];
    f.open       = true;
    f.mount_idx  = r.mount_idx;
    f.node_id    = r.node_id;
    f.flags      = flags;
    f.pos        = (flags & VFS_O_APPEND) ? inst->driver->fs_size(inst, r.node_id) : 0;
    return fd;
}

VFS_Error vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD) return VFS_ERR_BAD_FD;
    if (!g_fds[fd].open)            return VFS_ERR_BAD_FD;
    g_fds[fd].open = false;
    return VFS_OK;
}

int vfs_read(int fd, void* buf, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD || !g_fds[fd].open) return -(int)VFS_ERR_BAD_FD;
    if (!buf) return -(int)VFS_ERR_BAD_ARG;
    if ((g_fds[fd].flags & 0x03) == VFS_O_WRONLY) return -(int)VFS_ERR_PERM;

    FileDesc& f = g_fds[fd];
    VFS_FSInstance* inst = &g_mounts[f.mount_idx].inst;

    uint32_t file_size = inst->driver->fs_size(inst, f.node_id);
    if (f.pos >= file_size) return 0;

    uint32_t avail   = file_size - f.pos;
    uint32_t to_read = (size < avail) ? size : avail;

    static uint8_t tmp_buf[65536];
    uint32_t rd = (file_size <= sizeof(tmp_buf)) ? file_size : (uint32_t)sizeof(tmp_buf);

    VFS_Error err = inst->driver->fs_read(inst, f.node_id, tmp_buf, &rd);
    if (err != VFS_OK) return -(int)err;

    if (f.pos >= rd) return 0;
    uint32_t available_in_buf = rd - f.pos;
    uint32_t copy = (to_read < available_in_buf) ? to_read : available_in_buf;

    uint8_t* dst = (uint8_t*)buf;
    uint8_t* src = tmp_buf + f.pos;
    for (uint32_t i = 0; i < copy; i++) dst[i] = src[i];
    f.pos += copy;
    return (int)copy;
}

int vfs_write(int fd, const void* buf, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD || !g_fds[fd].open) return -(int)VFS_ERR_BAD_FD;
    if (!buf) return -(int)VFS_ERR_BAD_ARG;
    if ((g_fds[fd].flags & 0x03) == VFS_O_RDONLY) return -(int)VFS_ERR_PERM;

    FileDesc& f = g_fds[fd];
    VFS_FSInstance* inst = &g_mounts[f.mount_idx].inst;

    VFS_Error err = inst->driver->fs_write(inst, f.node_id, buf, size);
    if (err != VFS_OK) return -(int)err;
    f.pos += size;
    return (int)size;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FD || !g_fds[fd].open) return -(int)VFS_ERR_BAD_FD;
    FileDesc& f = g_fds[fd];
    VFS_FSInstance* inst = &g_mounts[f.mount_idx].inst;
    uint32_t file_size = inst->driver->fs_size(inst, f.node_id);

    int32_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset; break;
        case VFS_SEEK_CUR: new_pos = (int32_t)f.pos + offset; break;
        case VFS_SEEK_END: new_pos = (int32_t)file_size + offset; break;
        default: return -(int)VFS_ERR_BAD_ARG;
    }
    if (new_pos < 0) new_pos = 0;
    f.pos = (uint32_t)new_pos;
    return (int)f.pos;
}

VFS_Error vfs_stat(int fd, VFS_Stat* out) {
    if (fd < 0 || fd >= VFS_MAX_FD || !g_fds[fd].open) return VFS_ERR_BAD_FD;
    if (!out) return VFS_ERR_BAD_ARG;
    FileDesc& f = g_fds[fd];
    VFS_FSInstance* inst = inst_for(f.mount_idx, f.node_id);

    const char* n = inst->driver->fs_name(inst, f.node_id);
    strncpy(out->name, n ? n : "", VFS_MAX_NAME - 1);
    out->name[VFS_MAX_NAME - 1] = '\0';
    out->size = inst->driver->fs_size(inst, f.node_id);
    out->type = inst->driver->fs_type(inst, f.node_id);
    return VFS_OK;
}

VFS_Error vfs_creat(const char* path, uint8_t type) {
    if (!path || !*path) return VFS_ERR_BAD_ARG;

    ParentResult pr = vfs_resolve_parent(path);
    if (!pr.ok) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = inst_for(pr.mount_idx, pr.parent_id);
    return inst->driver->fs_create(inst, pr.name, type, pr.parent_id);
}

VFS_Error vfs_mkdir(const char* path) {
    return vfs_creat(path, VFS_ENT_DIR);
}

VFS_Error vfs_unlink(const char* path) {
    if (!path || !*path) return VFS_ERR_BAD_ARG;
    ResolveResult r = vfs_resolve(path);
    if (!r.found) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    uint8_t t = (r.node_id == -1) ? VFS_ENT_DIR : inst->driver->fs_type(inst, r.node_id);
    if (t == VFS_ENT_DIR) return VFS_ERR_NOT_FILE;

    return inst->driver->fs_remove(inst, r.node_id);
}

VFS_Error vfs_rmdir(const char* path) {
    if (!path || !*path) return VFS_ERR_BAD_ARG;

    char abs[VFS_MAX_PATH];
    path_absolute(path, abs, VFS_MAX_PATH);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].path, abs) == 0)
            return VFS_ERR_BUSY;
    }

    ResolveResult r = vfs_resolve(path);
    if (!r.found) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    uint8_t t = (r.node_id == -1) ? VFS_ENT_DIR : inst->driver->fs_type(inst, r.node_id);
    if (t != VFS_ENT_DIR) return VFS_ERR_NOT_DIR;

    VFS_Dir d = inst->driver->fs_readdir(inst, r.node_id);
    if (d.count > 0) return VFS_ERR_NOT_EMPTY;

    return inst->driver->fs_remove(inst, r.node_id);
}

VFS_Error vfs_rename(const char* old_path, const char* new_path) {
    (void)old_path; (void)new_path;
    return VFS_ERR_NOT_IMPL;
}

VFS_Error vfs_read_file(const char* path, void* buf, uint32_t* inout_size) {
    if (!path || !*path || !buf || !inout_size) return VFS_ERR_BAD_ARG;
    ResolveResult r = vfs_resolve(path);
    if (!r.found) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    if (!inst || !inst->driver) {
        log(1, "VFS read_file '%s': NULL driver (mount_idx=%d node_id=%d)",
            path, r.mount_idx, r.node_id);
        return VFS_ERR_NO_FS;
    }
    uint32_t fsz = inst->driver->fs_size(inst, r.node_id);
    return inst->driver->fs_read(inst, r.node_id, buf, inout_size);
}

VFS_Error vfs_write_file(const char* path, const void* buf, uint32_t size) {
    if (!path || !*path || !buf) return VFS_ERR_BAD_ARG;
    ResolveResult r = vfs_resolve(path);
    if (!r.found) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    return inst->driver->fs_write(inst, r.node_id, buf, size);
}

VFS_Error vfs_stat_path(const char* path, VFS_Stat* out) {
    if (!path || !*path || !out) return VFS_ERR_BAD_ARG;
    ResolveResult r = vfs_resolve(path);
    if (!r.found) return VFS_ERR_NOT_FOUND;

    VFS_FSInstance* inst = rr_inst(r);
    if (r.node_id == -1) {
        out->name[0] = '/'; out->name[1] = '\0';
        out->size = 0;
        out->type = VFS_ENT_DIR;
    } else {
        const char* n = inst->driver->fs_name(inst, r.node_id);
        strncpy(out->name, n ? n : "", VFS_MAX_NAME - 1);
        out->name[VFS_MAX_NAME - 1] = '\0';
        out->size = inst->driver->fs_size(inst, r.node_id);
        out->type = inst->driver->fs_type(inst, r.node_id);
    }
    return VFS_OK;
}

VFS_Dir vfs_readdir(const char* path) {
    VFS_Dir d; d.count = 0; d.error = VFS_OK;
    if (!path || !*path) { d.error = VFS_ERR_BAD_ARG; return d; }

    ResolveResult r = vfs_resolve(path);
    if (!r.found) { d.error = VFS_ERR_NOT_FOUND; return d; }

    VFS_FSInstance* inst = rr_inst(r);

    uint8_t t = (r.node_id == -1) ? VFS_ENT_DIR : inst->driver->fs_type(inst, r.node_id);
    if (t != VFS_ENT_DIR) { d.error = VFS_ERR_NOT_DIR; return d; }

    VFS_Dir raw = inst->driver->fs_readdir(inst, r.node_id);

    char dir_abs[VFS_MAX_PATH];
    path_absolute(path, dir_abs, VFS_MAX_PATH);
    int dir_abs_len = 0; while (dir_abs[dir_abs_len]) dir_abs_len++;

    bool added[VFS_MAX_MOUNTS] = {};
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) continue;
        if (!path_starts_with(g_mounts[i].path, dir_abs)) continue;
        const char* suffix = g_mounts[i].path + dir_abs_len;
        while (*suffix == '/') suffix++;
        if (!*suffix) continue;
        bool direct = true;
        for (const char* s = suffix; *s; s++) {
            if (*s == '/') { direct = false; break; }
        }
        if (!direct) continue;
        bool already = false;
        for (int j = 0; j < raw.count; j++) {
            if (strcmp(raw.entries[j].name, suffix) == 0) { already = true; break; }
        }
        if (!already && raw.count < VFS_MAX_DIR_ENTRIES) {
            strncpy(raw.entries[raw.count].name, suffix, VFS_MAX_NAME - 1);
            raw.entries[raw.count].name[VFS_MAX_NAME - 1] = '\0';
            raw.entries[raw.count].size = 0;
            raw.entries[raw.count].type = VFS_ENT_DIR;
            raw.count++;
        }
    }
    return raw;
}

bool vfs_cd(const char* path) {
    if (!path || !*path) return false;

    char abs[VFS_MAX_PATH];
    path_absolute(path, abs, VFS_MAX_PATH);

    ResolveResult r = vfs_resolve(abs);
    if (!r.found) return false;

    VFS_FSInstance* inst = rr_inst(r);
    uint8_t t = (r.node_id == -1) ? VFS_ENT_DIR : inst->driver->fs_type(inst, r.node_id);
    if (t != VFS_ENT_DIR) return false;

    g_cwd_mount = r.mount_idx;
    g_cwd_id    = r.node_id;

    int i = 0;
    while (abs[i] && i < VFS_MAX_PATH - 1) { g_cwd_abs[i] = abs[i]; i++; }
    g_cwd_abs[i] = '\0';
    return true;
}

void vfs_pwd() {
    printg("%s", vfs_getcwd());
}

const char* vfs_getcwd() {
    return g_cwd_abs;
}

void vfs_dump_mounts() {
    log(3, "VFS: mount table:");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) continue;
        if (g_mounts[i].bind) {
            log(3, "  [%d] bind  '%s' - slot %d id %d",
                i, g_mounts[i].path,
                g_mounts[i].bind_mount_idx,
                g_mounts[i].bind_dir_id);
        } else {
            log(3, "  [%d] fs    '%s'  drv='%s'",
                i, g_mounts[i].path,
                g_mounts[i].inst.driver
                    ? g_mounts[i].inst.driver->name : "?");
        }
    }
}

const char* vfs_strerror(VFS_Error err) {
    switch (err) {
        case VFS_OK:            return "OK";
        case VFS_ERR_NOT_FOUND: return "not found";
        case VFS_ERR_EXISTS:    return "already exists";
        case VFS_ERR_NO_SPACE:  return "no space";
        case VFS_ERR_NOT_FILE:  return "not a file";
        case VFS_ERR_NOT_DIR:   return "not a directory";
        case VFS_ERR_NO_FS:     return "no filesystem mounted";
        case VFS_ERR_BAD_ARG:   return "bad argument";
        case VFS_ERR_IO:        return "I/O error";
        case VFS_ERR_TOO_BIG:   return "data too large";
        case VFS_ERR_NO_FD:     return "no free file descriptors";
        case VFS_ERR_BAD_FD:    return "bad file descriptor";
        case VFS_ERR_NOT_IMPL:  return "not implemented";
        case VFS_ERR_NOT_EMPTY: return "directory not empty";
        case VFS_ERR_BUSY:      return "resource busy";
        case VFS_ERR_PERM:      return "permission denied";
        default:                return "unknown error";
    }
}
