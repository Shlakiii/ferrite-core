#include "shell.h"
#include "format.h"

#include <func/vfs.h>
#include <func/scrollbuf.h>
#include <func/sbil.h>

#include <lib/strtools/string.h>
#include <lib/strtools/ctype.h>
#include <lib/rmem/memory.h>
#include <lib/metad.h>

#include <io/printg.h>
#include <io/keyboard.h>
#include <io/fb.h>

#include <data/logo.h>

#include <drivers/hw/ata_pio.h>
#include <drivers/hw/ata_diskio.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <func/lua/lua_runtime.h>

#include <test/test_prgs.h>

#define HISTORY_SIZE 32
#define MAX_CMD_LEN  256

#define TEST false

bool shrboot = false;
bool shsdown = false;

static User* find_user(const char* name) {
    for (int i = 0; i < users_count; i++) {
        if (strcmp(users_list[i].name, name) == 0) {
            return &users_list[i];
        }
    }
    return nullptr;
}

static User* current_session_user = nullptr; 

#define is_root (current_session_user && current_session_user->access > 0)
#define current_user (current_session_user ? current_session_user->name : "unknown")

static void set_user(const char* name) {
    User* user = find_user(name);
    if (user) {
        current_session_user = user;
    } else {
        current_session_user = &users_list[0];
    }
}

static char history[HISTORY_SIZE][MAX_CMD_LEN];
static int  history_count = 0;
static int  history_pos   = 0;

static void history_add(const char* cmd) {
    if (!cmd || !*cmd) return;
    strncpy(history[history_count % HISTORY_SIZE], cmd, MAX_CMD_LEN-1);
    history[history_count % HISTORY_SIZE][MAX_CMD_LEN-1] = '\0';
    history_count++;
    history_pos = history_count;
}

static int parse_int(const char* s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

static void print_prompt() {
    printgc(1,  0, "%s" PG_NNL, current_user);
    if (is_root) {
        printgc(7, 8, "[R]" PG_NNL);
    } else {
        printgc(7, 8, "[U]" PG_NNL);
    }
    printgc(8,  0, "@QMS[" PG_NNL);
    printgc(14, 0, "%s" PG_NNL, vfs_getcwd());
    printgc(8, 0, "]" PG_NNL);
    printgc(7,  0, "# " PG_NNL);
}

typedef void (*command_func_t)(char* args, bool is_elevated);

struct ShellCommand {
    const char* name;
    command_func_t func;
    const char* help;
};

static bool shell_running = true;

static void cmd_help(char* args, bool is_elevated);
static void cmd_whoami(char* args, bool is_elevated);
static void cmd_ls(char* args, bool is_elevated);
static void cmd_cd(char* args, bool is_elevated);
static void cmd_pwd(char* args, bool is_elevated);
static void cmd_touch(char* args, bool is_elevated);
static void cmd_setattr(char* args, bool is_elevated);
static void cmd_mkdir(char* args, bool is_elevated);
static void cmd_rm(char* args, bool is_elevated);
static void cmd_cat(char* args, bool is_elevated);
static void cmd_run(char* args, bool is_elevated);
static void cmd_su(char* args, bool is_elevated);
static void cmd_clear(char* args, bool is_elevated);
static void cmd_mem(char* args, bool is_elevated);
static void cmd_sinfo(char* args, bool is_elevated);
static void cmd_about(char* args, bool is_elevated);
static void cmd_run_lua(char* args, bool is_elevated);
void cmd_mount(char* args, bool is_elevated);
static void cmd_umount(char* args, bool is_elevated);
static void cmd_dd(char* args, bool is_elevated);
static void cmd_format(char* args, bool is_elevated);
static void cmd_lsblk(char* args, bool is_elevated);
static void cmd_moudump(char* args, bool is_elevated);
static void cmd_cp(char* args, bool is_elevated);
static void cmd_mv(char* args, bool is_elevated);
static const char* path_basename(const char* path);
static void path_join(char* out, size_t out_size, const char* dir, const char* name);
static void sreboot(char* args, bool is_elevated);
static void sshutdown(char* args, bool is_elevated);
static void cmd_nukdir(char* args, bool is_elevated);
static void cmd_callt(char* args, bool is_elevated);

static ShellCommand commands[] = {
    {"help",    cmd_help,    "Show this help"},
    {"whoami",  cmd_whoami,  "Show user info"},
    {"ls",      cmd_ls,      "List directory"},
    {"cd",      cmd_cd,      "Change directory"},
    {"pwd",     cmd_pwd,     "Print working directory"},
    {"touch",   cmd_touch,   "Create an empty file"},
    {"setattr", cmd_setattr, "Set file attributes"},
    {"mkdir",   cmd_mkdir,   "Create directory"},
    {"rm",      cmd_rm,      "Remove file or directory"},
    {"nukedir", cmd_nukdir,   "Remove directory recursively (DANGEROUS!)"},
    {"cp",      cmd_cp,      "Copy file: cp <src> <dst>"},
    {"mv",      cmd_mv,      "Move/rename file: mv <src> <dst>"},
    {"cat",     cmd_cat,     "Display file content"},
    {"run",     cmd_run,     "Run .qex application (-s = as root)"},
    {"run_lua", cmd_run_lua, "Run Lua script: run_lua <script>"},
    {"su",      cmd_su,      "Switch to root user"},
    {"lsblk",   cmd_lsblk,   "List block devices (ATA drives)"},
    {"mount",   cmd_mount,   "Mount filesystem: mount <disk> <mountpoint>"},
    {"umount",  cmd_umount,  "Unmount filesystem: umount <mountpoint>"},
    {"dd",      cmd_dd,      "Disk dump: dd if=<disk> of=<file> bs=512 count=N skip=N"},
    {"format",  cmd_format,  "Format partition: format <disk> [fat16|fat32]  (DANGEROUS!)"},
    {"moudump", cmd_moudump, "Show mount list"},
    {"clear",   cmd_clear,   "Clear screen"},
    {"mem",     cmd_mem,     "Memory usage"},
    {"sinfo",   cmd_sinfo,   "System information"},
    {"about",   cmd_about,   "About QMS OS"},
    {"callt",   cmd_callt,   "Call test program <test_code>"},
    {"reboot",  sreboot,     "Reboot system"},
    {"shutdown",sshutdown,   "Shutdown system"},
    {nullptr, nullptr, nullptr}
};

void execute_command(char* line, bool is_elevated) {
    vfs_set_current_user(is_elevated);
    char* args = NULL;
    char* p = line;
    while (*p && !isspace(*p)) p++;
    if (*p) {
        *p = '\0';
        args = p + 1;
    }

    for (int i = 0; commands[i].name; i++) {
        if (strcmp(line, commands[i].name) == 0) {
            commands[i].func(args, is_elevated);
            vfs_set_current_user(is_root);
            return;
        }
    }
    vfs_set_current_user(is_root);
    printgc(12, 0, "Unknown command: %s", line);
}

static void cmd_help(char* args, bool is_elevated) {
    printgc(15, 0, "Available commands:");
    for (int i = 0; commands[i].name; i++) {
        printgc(7, 0, "    %s  %s", commands[i].name, commands[i].help);
    }
}

static void cmd_moudump(char* args, bool is_elevated) {
    vfs_dump_mounts();
}

static void cmd_whoami(char* args, bool is_elevated) {
    if (args && strcmp(args, "list") == 0) {
        printgc(15, 0, "  REGISTERED USERS  ");
        printg("");
        
        for (int i = 0; i < users_count; i++) {
            printgc(15, 0, " %s " PG_NNL, users_list[i].name);

            if (users_list[i].access > 0) {
                printgc(12, 0, "[ADMIN]");
            } else {
                printgc(10, 0, "[USER]");
            }
        }
        printg("");
        return;
    }

    printgc(15, 0, "  USER INFO  ");
    printg("");

    printgc(15, 1, " USER " PG_NNL);
    printgc(0, 7, " %s ", current_user, PG_NNL);

    if (is_root || is_elevated) {
        printgc(15, 4, " ROOT " PG_NNL);
        printgc(0, 7, " Administrator Access " PG_NNL);
    } else {
        printgc(15, 2, " USER " PG_NNL);
        printgc(0, 7, " Restricted Access " PG_NNL);
    }
    
    printg("\n");
}

static void cmd_ls(char* args, bool is_elevated) {
    VFS_Dir dir = vfs_readdir(args && *args ? args : ".");
    if (dir.error != VFS_OK) {
        printg("ls: %s", vfs_strerror(dir.error));
        return;
    }
    for (int i = 0; i < dir.count; i++) {
        if (dir.entries[i].type == VFS_ENT_DIR)
            printgc(3, 0, "  %s/" PG_NNL, dir.entries[i].name);
        else
            printgc(7, 0, "  %s" PG_NNL, dir.entries[i].name);
    }
    printg("");
}

static void cmd_run_lua(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: run_lua <script>"); return; }
    lua_run_luac(args);
}

static void cmd_cd(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: cd <directory>"); return; }
    if (!vfs_cd(args)) printg("cd: no such directory");
}

static void cmd_nukdir(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: nukedir <directory>"); return; }
    VFS_Error err = vfs_rmdir(args);
    if (err && err == VFS_ERR_NOT_EMPTY) {
        vfs_cd(args);
        VFS_Dir d = vfs_readdir(".");
        for (int i = 0; i < d.count; i++) {
            if (strcmp(d.entries[i].name, ".") == 0 || strcmp(d.entries[i].name, "..") == 0) continue;
            char childpath[VFS_MAX_PATH];
            path_join(childpath, VFS_MAX_PATH, vfs_getcwd(), d.entries[i].name);
            if (d.entries[i].type == VFS_ENT_DIR) {
                cmd_nukdir(childpath, is_elevated);
            } else {
                VFS_Error e = vfs_unlink(childpath);
                if (e != VFS_OK) printg("nukedir: %s", vfs_strerror(e));
            }
        }
        vfs_cd("/");
        err = vfs_rmdir(args);
    } else if (err != VFS_OK) {
        printg("nukedir: %s", vfs_strerror(err));
    }
}

static void cmd_pwd(char* args, bool is_elevated) {
    printg("%s", vfs_getcwd());
}

static void cmd_touch(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: touch <path>"); return; }
    while (*args == ' ' || *args == '\t') args++;
    VFS_Stat stat;
    VFS_Error err = vfs_stat_path(args, &stat);
    if (err == VFS_OK) return;
    if (err != VFS_ERR_NOT_FOUND) {
        printg("touch: %s", vfs_strerror(err));
        return;
    }
    err = vfs_creat(args, VFS_ENT_FILE);
    if (err != VFS_OK) printg("touch: %s", vfs_strerror(err));
}

static void cmd_setattr(char* args, bool is_elevated) {
    if (!args || !*args) {
        printg("Usage: setattr <path> [-sys] [+sys] [-hide] [+hide]");
        return;
    }
    while (*args == ' ' || *args == '\t') args++;
    char* attributes = args;
    while (*attributes && *attributes != ' ' && *attributes != '\t') attributes++;
    if (!*attributes) {
        printg("Usage: setattr <path> [-sys] [+sys] [-hide] [+hide]");
        return;
    }
    *attributes++ = '\0';
    while (*attributes == ' ' || *attributes == '\t') attributes++;
    if (!*attributes) {
        printg("Usage: setattr <path> [-sys] [+sys] [-hide] [+hide]");
        return;
    }
    VFS_Error err = vfs_setattribute(args, attributes);
    if (err != VFS_OK) printg("setattr: %s", vfs_strerror(err));
}

static void cmd_mkdir(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: mkdir <name>"); return; }
    VFS_Error err = vfs_mkdir(args);
    if (err != VFS_OK) printg("mkdir: %s", vfs_strerror(err));
}

static void cmd_rm(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: rm <name>"); return; }
    VFS_Error err = vfs_unlink(args);
    if (err != VFS_OK) printg("rm: %s", vfs_strerror(err));
}

static const char* path_basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' && *(p+1) != '\0') last = p + 1;
    return last;
}

static void path_join(char* out, size_t out_size, const char* dir, const char* name) {
    size_t dlen = strlen(dir);
    bool has_slash = (dlen > 0 && dir[dlen-1] == '/');
    if (has_slash)
        snprintf(out, out_size, "%s%s", dir, name);
    else
        snprintf(out, out_size, "%s/%s", dir, name);
}

static void cmd_cp(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: cp <src> <dst>"); return; }

    char src[VFS_MAX_PATH] = {0};
    char dst[VFS_MAX_PATH] = {0};

    char* sep = strchr(args, ' ');
    if (!sep) { printg("Usage: cp <src> <dst>"); return; }
    size_t src_len = (size_t)(sep - args);
    if (src_len >= VFS_MAX_PATH) { printg("cp: source path too long"); return; }
    strncpy(src, args, src_len);
    char* d = sep + 1;
    while (*d == ' ') d++;
    if (!*d) { printg("Usage: cp <src> <dst>"); return; }
    strncpy(dst, d, VFS_MAX_PATH - 1);

    struct VFS_Stat src_st;
    VFS_Error err = vfs_stat_path(src, &src_st);
    if (err != VFS_OK) { printg("cp: %s: %s", src, vfs_strerror(err)); return; }
    if (src_st.type != VFS_ENT_FILE) { printg("cp: %s: not a file", src); return; }

    char real_dst[VFS_MAX_PATH] = {0};
    struct VFS_Stat dst_st;
    if (vfs_stat_path(dst, &dst_st) == VFS_OK && dst_st.type == VFS_ENT_DIR) {
        path_join(real_dst, VFS_MAX_PATH, dst, path_basename(src));
    } else {
        strncpy(real_dst, dst, VFS_MAX_PATH - 1);
    }

    struct VFS_Stat rdst_st;
    if (vfs_stat_path(real_dst, &rdst_st) != VFS_OK) {
        err = vfs_creat(real_dst, VFS_ENT_FILE);
        if (err != VFS_OK) { printg("cp: %s: %s", real_dst, vfs_strerror(err)); return; }
    }

    if (src_st.size == 0) return;

    void* buf = kernel_mem_manager->allocate(src_st.size);
    if (!buf) { printg("cp: out of memory"); return; }

    uint32_t sz = src_st.size;
    err = vfs_read_file(src, buf, &sz);
    if (err != VFS_OK) {
        printg("cp: read error: %s", vfs_strerror(err));
        kernel_mem_manager->free(buf, src_st.size);
        return;
    }

    err = vfs_write_file(real_dst, buf, sz);
    kernel_mem_manager->free(buf, src_st.size);
    if (err != VFS_OK) printg("cp: write error: %s", vfs_strerror(err));
}

static void cmd_callt(char* args, bool is_elevated) {
    if(!TEST)return;
    
    printg("Ready");
    //if(!is_elevated)printg("no right");return;
    //if(!args || !*args)printg("usage: callt <test_code>");return;
    if(args){
        printg("Running");
        otval_test();
    }else{
        printg("Invalid test code");
    }
}

static bool mv_file(const char* src, const char* real_dst) {
    VFS_Error err;
    struct VFS_Stat st;
    err = vfs_stat_path(src, &st);
    if (err != VFS_OK) { printg("mv: %s: %s", src, vfs_strerror(err)); return false; }

    struct VFS_Stat rdst_st;
    if (vfs_stat_path(real_dst, &rdst_st) != VFS_OK) {
        err = vfs_creat(real_dst, VFS_ENT_FILE);
        if (err != VFS_OK) { printg("mv: %s: %s", real_dst, vfs_strerror(err)); return false; }
    }

    if (st.size > 0) {
        void* buf = kernel_mem_manager->allocate(st.size);
        if (!buf) { printg("mv: out of memory"); return false; }

        uint32_t sz = st.size;
        err = vfs_read_file(src, buf, &sz);
        if (err != VFS_OK) {
            printg("mv: read error: %s", vfs_strerror(err));
            kernel_mem_manager->free(buf, st.size);
            return false;
        }

        err = vfs_write_file(real_dst, buf, sz);
        kernel_mem_manager->free(buf, st.size);
        if (err != VFS_OK) {
            printg("mv: write error: %s", vfs_strerror(err));
            return false;
        }
    }

    err = vfs_unlink(src);
    if (err != VFS_OK)
        printg("mv: warning: could not remove source: %s", vfs_strerror(err));
    return true;
}

static bool mv_dir(const char* src, const char* real_dst);
static bool mv_dir(const char* src, const char* real_dst) {
    VFS_Error err;

    struct VFS_Stat rdst_st;
    if (vfs_stat_path(real_dst, &rdst_st) != VFS_OK) {
        err = vfs_mkdir(real_dst);
        if (err != VFS_OK) { printg("mv: mkdir %s: %s", real_dst, vfs_strerror(err)); return false; }
    }

    VFS_Dir dir = vfs_readdir(src);
    if (dir.error != VFS_OK) { printg("mv: readdir %s: %s", src, vfs_strerror(dir.error)); return false; }

    bool ok = true;
    for (int i = 0; i < dir.count; i++) {
        const VFS_DirEntry& e = dir.entries[i];
        if (e.name[0] == '.' && (e.name[1] == ' ' || (e.name[1] == '.' && e.name[2] == ' ')))
            continue;

        char child_src[VFS_MAX_PATH] = {0};
        char child_dst[VFS_MAX_PATH] = {0};
        path_join(child_src, VFS_MAX_PATH, src,      e.name);
        path_join(child_dst, VFS_MAX_PATH, real_dst, e.name);

        if (e.type == VFS_ENT_FILE) {
            if (!mv_file(child_src, child_dst)) ok = false;
        } else if (e.type == VFS_ENT_DIR) {
            if (!mv_dir(child_src, child_dst)) ok = false;
        }
    }

    err = vfs_rmdir(src);
    if (err != VFS_OK) {
        printg("mv: warning: could not remove source dir %s: %s", src, vfs_strerror(err));
        ok = false;
    }
    return ok;
}

static void cmd_mv(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: mv <src> <dst>"); return; }

    char src[VFS_MAX_PATH] = {0};
    char dst[VFS_MAX_PATH] = {0};

    char* sep = strchr(args, ' ');
    if (!sep) { printg("Usage: mv <src> <dst>"); return; }
    size_t src_len = (size_t)(sep - args);
    if (src_len >= VFS_MAX_PATH) { printg("mv: source path too long"); return; }
    strncpy(src, args, src_len);
    char* d = sep + 1;
    while (*d == ' ') d++;
    if (!*d) { printg("Usage: mv <src> <dst>"); return; }
    strncpy(dst, d, VFS_MAX_PATH - 1);

    struct VFS_Stat src_st;
    VFS_Error err = vfs_stat_path(src, &src_st);
    if (err != VFS_OK) { printg("mv: %s: %s", src, vfs_strerror(err)); return; }

    struct VFS_Stat dst_st;
    VFS_Error dst_err = vfs_stat_path(dst, &dst_st);

    char real_dst[VFS_MAX_PATH] = {0};

    if (dst_err == VFS_OK && dst_st.type == VFS_ENT_DIR) {
        path_join(real_dst, VFS_MAX_PATH, dst, path_basename(src));
    } else {
        strncpy(real_dst, dst, VFS_MAX_PATH - 1);
    }

    if (src_st.type == VFS_ENT_DIR) {
        mv_dir(src, real_dst);
    } else {
        mv_file(src, real_dst);
    }
}

static void cmd_cat(char* args, bool is_elevated) {
    if (!args || !*args) { printg("Usage: cat <file>"); return; }

    for (const char* p = args; *p; p++) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0') &&
            (p == args || p[-1] == '/')) {
            printgc(12, 0, "cat: path traversal not allowed");
            return;
        }
    }

    struct VFS_Stat st;
    if (vfs_stat_path(args, &st) != VFS_OK) {
        printg("cat: %s: not found", args);
        return;
    }
    if (st.size == 0) { printg("(empty file)"); return; }

    char* buf = (char*)kernel_mem_manager->allocate(st.size + 1);
    if (!buf) { printg("cat: out of memory"); return; }

    uint32_t sz = st.size;
    if (vfs_read_file(args, buf, &sz) == VFS_OK) {
        buf[sz] = '\0';
        printg("%s", buf);
    }
    kernel_mem_manager->free(buf, st.size + 1);
}

static void cmd_run(char* args, bool is_elevated) {
    if (!args || !*args) {
        printg("Usage: run <path.qex> [arguments...]");
        return;
    }

    char* space = strchr(args, ' ');
    char* app_path = args;
    char* user_arg = NULL;

    if (space) {
        *space = '\0';
        user_arg = space + 1;
        while (*user_arg == ' ') user_arg++;
    }

    {
        const char* dot = nullptr;
        for (const char* p = app_path; *p; p++)
            if (*p == '.') dot = p;
        if (!dot || strcmp(dot, ".qex") != 0) {
            printgc(12, 0, "run: only .qex files are executable");
            return;
        }
        for (const char* p = app_path; *p; p++) {
            if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0') &&
                (p == app_path || p[-1] == '/')) {
                printgc(12, 0, "run: path traversal not allowed");
                return;
            }
        }
    }

    if (user_arg && *user_arg) {
        sbil_set_arg(user_arg);
    } else {
        sbil_set_arg("");
    }

    SbilAppDesc desc;
    SbilError err = sbil_exec(app_path, is_elevated ? 1 : 0, &desc);

    sbil_clear_arg();

    if (err != SBIL_OK)
        printgc(12, 0, "Execution failed: %s", sbil_strerror(err));
}

static bool ask_password() {
    printgc(7, 0, " Password: " PG_NNL);
    char input[MAX_CMD_LEN];
    memset(input, 0, sizeof(input));
    int cursor = 0;
    while (1) {
        char key = get_char();
        if (key == '\n') {
            input[cursor] = '\0';
            print_raw('\n', 0x7, 0x0);
            break;
        }
        if (key == '\b' && cursor > 0) {
            cursor--;
            input[cursor] = '\0';
            goto_xy(cursor_x - 1, cursor_y);
            print_raw(' ', 0x7, 0x0);
            goto_xy(cursor_x - 1, cursor_y);
        } else if (isprint(key) && cursor < MAX_CMD_LEN - 1) {
            input[cursor++] = key;
            print_raw('-', 0x7, 0x0);
        }
    }
    bool ok = (strcmp(input, PASSWORD) == 0);
    memset(input, 0, sizeof(input));
    return ok;
}

static void cmd_su(char* args, bool is_elevated) {
    if (!args || !*args) {
        printgc(7, 0, "Usage: su <command> \n       su -s <username>");
        return;
    }

    if (strncmp(args, "-s ", 3) == 0) {
        char* target_name = args + 3;
        while (isspace(*target_name)) target_name++;

        User* target = find_user(target_name);
        if (!target) {
            printgc(12, 0, " Unknown user: %s", target_name);
            return;
        }

        if (target->access > 0 && is_root) {
            if (!ask_password()) {
                printgc(12, 0, " Access denied.");
                return;
            }
        }

        set_user(target->name);
        printgc(10, 0, " Session switched to %s.", target->name);
        return;
    }

    if (!is_root) {
        if (!ask_password()) {
            printgc(12, 0, " Authentication failure.");
            return;
        }
    }
    execute_command(args, true);
}

static void cmd_clear(char* args, bool is_elevated) { clear_screen(); }

static void print_mem_bar(size_t used, size_t total, int width) {
    int filled = (total > 0) ? (int)((used * (size_t)width) / total) : 0;
    printgc(8, 0, "[" PG_NNL);
    for (int i = 0; i < width; i++) {
        if (i < filled) printgc(2, 2, "#" PG_NNL);
        else            printgc(8, 8, "." PG_NNL);
    }
    printgc(8, 0, "]" PG_NNL);
}

void cmd_mem(char* args, bool is_elevated) {
    MemoryStats ks = kernel_mem_manager->get_stats();
    MemoryStats us = user_mem_manager->get_stats();
    MemoryStats ls = lua_mem_manager->get_stats();
    printgc(8, 0,"");
    printgc(7, 0, "Kernel heap:" PG_NNL);
    print_mem_bar(ks.used_bytes, ks.total_bytes, 32);
    printgc(7, 0, " %d / %d KB", (int)(ks.used_bytes/1024), (int)(ks.total_bytes/1024));
    printgc(8, 0, "  Free  : %d KB", (int)(ks.free_bytes / 1024));
    printgc(8, 0, "  Allocs: %d active, %d slab, %d page", (int)ks.alloc_count, (int)ks.slab_allocs, (int)ks.page_allocs);
    printgc(8, 0,"");
    printgc(7, 0, "User heap:" PG_NNL);
    print_mem_bar(us.used_bytes, us.total_bytes, 32);
    printgc(7, 0, " %d / %d KB", (int)(us.used_bytes/1024), (int)(us.total_bytes/1024));
    printgc(7, 0, "  Free  : %d KB", (int)(us.free_bytes / 1024));
    printgc(8, 0, "  Allocs: %d active, %d slab, %d page", (int)us.alloc_count, (int)us.slab_allocs, (int)us.page_allocs);
    printgc(8, 0,"");
    printgc(7, 0, "Lua heap:" PG_NNL);
    print_mem_bar(ls.used_bytes, ls.total_bytes, 32);
    printgc(7, 0, " %d / %d KB", (int)(ls.used_bytes/1024), (int)(ls.total_bytes/1024));
    printgc(7, 0, "  Free  : %d KB", (int)(ls.free_bytes / 1024));
    printgc(8, 0, "  Allocs: %d active, %d slab, %d page", (int)ls.alloc_count, (int)ls.slab_allocs, (int)ls.page_allocs);
    printgc(8, 0,"");
}

void cmd_sinfo(char* args, bool is_elevated) {
    show_gbanner(cursor_x, cursor_y);

    MemoryStats ks = kernel_mem_manager->get_stats();
    MemoryStats us = user_mem_manager->get_stats();
    printgc(15, 0, "System Information");
    printgc(11, 0, "[OS]");
    printgc(7,  0, "  Kernel : QCore %s %s", CORE_VER, BUILD_TAG, " (build " __DATE__ ")");
    printgc(7,  0, "  Shell  : %s v%s", SH_NAME, SH_VER);
    printgc(7,  0, "  Arch   : %s", CARCH);
    printgc(11, 0, "[Display]");
    printgc(7, 0, "  Mode : %dx%d chars", sw, sh);
    printgc(11, 0, "[ATA Drives]");
    {
        int cnt = ata_drive_count();
        if (cnt == 0) {
            printgc(8, 0, "  No ATA drives detected");
        } else {
            for (int i = 0; i < cnt; i++) {
                const AtaDrive* d = ata_get_drive(i);
                if (d && d->present) {
                    uint32_t sz_mb = d->lba_count / 2048;
                    printgc(7, 0, "  ATA%d: %s  [%u MB]", i, d->model, (unsigned)sz_mb);
                }
            }
        }
    }
    printgc(11, 0, "[Memory]");
    size_t total_ram = ks.total_bytes + us.total_bytes;
    size_t used_ram  = ks.used_bytes  + us.used_bytes;
    printgc(7,  0, "  Total  : %d KB", (int)(total_ram / 1024));
    printgc(7, 0, "  Free   : %d KB", (int)((total_ram - used_ram) / 1024));
    printgc(7, 0, "  Used   : %d KB", (int)(used_ram / 1024));
}

void cmd_about(char* args, bool is_elevated) {
    show_gbanner(cursor_x, cursor_y);

    printgc(15,0,"Ferrite Core");
    printgc(7,0,"  Kernel     : QMS Kernel");
    printgc(7,0,"  Shell      : %s v%s", SH_NAME, SH_VER);
    printgc(7,0,"  Version    : %s | %s | %s", FERR_VER, FERR_B_TA, FERR_BUILD_TAG);
    printgc(7,0,"  Build Date : %s",FERR_BUILD_DATE);
    printgc(7,0,"  Features   : LuaJIT, Execute Lua Code");
    printgc(8,0,"  License    : %s",FERR_LICENSE);
    printgc(7,0,"");

    printgc(15,0,"  Development Team:");
    printgc(7,0,"   Lead Developer: %s", FERR_AUTHOR);

    printgc(7, 0, "");

    printgc(15, 0, "QMS Kernel");
    printgc(7,  0, "  Kernel     : QCore %s %s", CORE_VER, BUILD_TAG);
    printgc(7,  0, "  Shell      : %s v%s", SH_NAME, SH_VER);
    printgc(7,  0, "  Arch       : %s", CARCH);
    printgc(7,  0, "  Build      : " __DATE__ " " __TIME__);
    printgc(8,  0, "  License    : %s", LICENSE);
    printgc(7,  0, ""); 

    printgc(15, 0, "  Development Team:"); 
    printgc(7,  0, "    Lead Developer: %s", AUTHOR);
    printgc(7,  0, "    Coder: C0000374");
    printgc(7,  0, "");
}

static void cmd_lsblk(char* args, bool is_elevated) {
    printgc(15, 0, "Block Devices:");
    int cnt = ata_drive_count();

    if (cnt == 0) {
        printgc(8, 0, "  No ATA drives detected.");
        return;
    }

    for (int i = 0; i < cnt; i++) {
        const AtaDrive* d = ata_get_drive(i);
        if (d && d->present) {
            uint32_t mb = d->lba_count / 2048;
            char safe_model[sizeof(d->model)];
            memcpy(safe_model, d->model, sizeof(safe_model) - 1);
            safe_model[sizeof(safe_model) - 1] = '\0';
            const char* name = (safe_model[0] && safe_model[0] != ' ')
                               ? safe_model : "(no model)";
            printgc(7, 0, "  ata%d   %s   %u MB   (%u sectors)",
                    i, name, (unsigned)mb, (unsigned)d->lba_count);
        } else {
            printgc(8, 0, "  ata%d   [not present]", i);
        }
    }
}

static AtaDiskIO* g_mount_disks[VFS_MAX_MOUNTS] = {};
static int        g_mount_disk_drives[VFS_MAX_MOUNTS] = {
    -1,-1,-1,-1,-1,-1,-1,-1
};

static AtaDiskIO* alloc_mount_disk(int drive) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (g_mount_disk_drives[i] == drive && g_mount_disks[i])
            return g_mount_disks[i];
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mount_disk_drives[i] == -1) {
            g_mount_disks[i]       = new AtaDiskIO(drive);
            g_mount_disk_drives[i] = drive;
            return g_mount_disks[i];
        }
    }
    return nullptr;
}

void cmd_mount(char* args, bool is_elevated) {
    if (!is_elevated) {
        printgc(12, 0, "mount: Permission denied (root required)");
        return;
    }

    if (!args || !*args) {
        printg("Usage: mount <device> <mountpoint>");
        printg("Examples:");
        printg("   mount ata0 /");
        printg("   mount ata1 /mnt/data");
        return;
    }

    char* device   = strtok(args, " ");
    char* mntpoint = strtok(NULL, " ");

    if (!device || !mntpoint) {
        printgc(12, 0, "mount: missing device or mountpoint");
        return;
    }

    int drive = -1;
    if (strncmp(device, "ata", 3) == 0)
        drive = parse_int(device + 3);

    if (drive < 0 || drive >= ata_drive_count()) {
        printgc(12, 0, "mount: unknown device '%s'", device);
        return;
    }

    AtaDiskIO* disk = alloc_mount_disk(drive);
    if (!disk) {
        printgc(12, 0, "mount: no free disk slots");
        return;
    }
    if (!disk->present()) {
        printgc(12, 0, "mount: %s not present", device);
        return;
    }

    printgc(7, 0, "Mounting %s as FAT32 on %s...", device, mntpoint);

    VFS_Error err = vfs_mount(mntpoint, "fat32", nullptr, 0, disk, 0, nullptr);

    if (err == VFS_OK)
        printgc(10, 0, "Mounted successfully: %s -> %s", device, mntpoint);
    else
        printgc(12, 0, "Mount failed: %s", vfs_strerror(err));
}

static void cmd_umount(char* args, bool is_elevated) {
    if (!is_elevated) {
        printgc(12, 0, "umount: Permission denied (root required)");
        return;
    }

    if (!args || !*args) {
        printg("Usage: umount <mountpoint>");
        printg("Example: umount /mnt/data");
        return;
    }

    VFS_Error err = vfs_umount(args);

    if (err == VFS_OK) {
        for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
            if (g_mount_disks[i] && g_mount_disk_drives[i] != -1) {
                bool still_used = false;
                int drv = g_mount_disk_drives[i];
                for (int j = 0; j < VFS_MAX_MOUNTS; j++) {
                    if (j != i && g_mount_disks[j] && g_mount_disk_drives[j] == drv) {
                        still_used = true;
                        break;
                    }
                }
                if (!still_used) {
                    delete g_mount_disks[i];
                    g_mount_disks[i]       = nullptr;
                    g_mount_disk_drives[i] = -1;
                }
            }
        }
        printgc(10, 0, "Unmounted %s", args);
    } else {
        printgc(12, 0, "umount failed: %s", vfs_strerror(err));
    }
}

static void cmd_dd(char* args, bool is_elevated) {
    if (!is_elevated) {
        printgc(12, 0, "dd: Permission denied (root required)");
        return;
    }

    if (!args || !*args) {
        printg("Usage: dd if=<device> of=<file> [skip=<N>] [count=<N>]");
        printg("Examples:");
        printg("   dd if=ata0 of=bootsect.bin count=1");
        printg("   dd if=ata1 of=disk.img skip=2048 count=10240");
        return;
    }

    char if_dev[32] = {0};
    char of_file[VFS_MAX_PATH] = {0};
    uint32_t skip_sectors = 0;
    uint32_t count_sectors = 1;

    char* token = strtok(args, " ");
    while (token) {
        if (strncmp(token, "if=", 3) == 0)
            strncpy(if_dev, token + 3, sizeof(if_dev)-1);
        else if (strncmp(token, "of=", 3) == 0)
            strncpy(of_file, token + 3, sizeof(of_file)-1);
        else if (strncmp(token, "skip=", 5) == 0)
            skip_sectors = (uint32_t)parse_int(token + 5);
        else if (strncmp(token, "count=", 6) == 0)
            count_sectors = (uint32_t)parse_int(token + 6);

        token = strtok(NULL, " ");
    }

    if (!if_dev[0] || !of_file[0]) {
        printgc(12, 0, "dd: 'if=' and 'of=' are required");
        return;
    }

    int drive = -1;
    if (strncmp(if_dev, "ata", 3) == 0)
        drive = parse_int(if_dev + 3);

    if (drive < 0 || drive >= ata_drive_count()) {
        printgc(12, 0, "dd: invalid device '%s'", if_dev);
        return;
    }

    AtaDiskIO disk(drive);
    if (!disk.present()) {
        printgc(12, 0, "dd: %s not present", if_dev);
        return;
    }

    if (count_sectors == 0 || count_sectors > (4u * 1024u * 1024u / ATA_SECTOR_SIZE)) {
        printgc(12, 0, "dd: too big (max 4MB, %u sectors)", 4u * 1024u * 1024u / ATA_SECTOR_SIZE);
        return;
    }
    uint32_t bytes = count_sectors * ATA_SECTOR_SIZE;

    void* buf = kernel_mem_manager->allocate(bytes);
    if (!buf) {
        printgc(12, 0, "dd: out of memory");
        return;
    }

    printgc(7, 0, "Reading %u sector(s) from %s (LBA %u)...", count_sectors, if_dev, skip_sectors);

    if (disk.read_sectors(skip_sectors, buf, (uint16_t)count_sectors)) {
        VFS_Error err = vfs_write_file(of_file, buf, bytes);
        if (err == VFS_OK)
            printgc(10, 0, "dd: %u bytes written to %s", bytes, of_file);
        else
            printgc(12, 0, "dd: write failed: %s", vfs_strerror(err));
    } else {
        printgc(12, 0, "dd: disk read error");
    }

    kernel_mem_manager->free(buf, bytes);
}

static void cmd_format(char* args, bool is_elevated) {
    if (!is_elevated) { printgc(12, 0, "format: Permission denied"); return; }
    if (!args || !*args) {
        printg("Usage: format <ataX> [fat32|fat16] [label]");
        printg("  fat32 (default, needs >= 32 MB)");
        printg("  fat16 (2-64 MB)");
        printgc(12, 0, "WARNING: This will ERASE all data on the device!");
        return;
    }
 
    char device[32]  = {0};
    char fsname[16]  = "fat32";
    char label[16]   = "QMS_VOLUME";
 
    char* tok = strtok(args, " ");
    if (tok) strncpy(device, tok, 31);
    tok = strtok(NULL, " ");
    if (tok) {
        if (strcmp(tok, "fat32") == 0 || strcmp(tok, "fat16") == 0) {
            strncpy(fsname, tok, 15);
            tok = strtok(NULL, " ");
        }
        if (tok) strncpy(label, tok, 15);
    }
    label[15] = '\0';
 
    int drive = -1;
    if (strncmp(device, "ata", 3) == 0) drive = parse_int(device + 3);
    if (drive < 0 || drive >= ata_drive_count()) {
        printgc(12, 0, "format: invalid device '%s'", device);
        return;
    }
 
    const AtaDrive* d = ata_get_drive(drive);
    if (!d || !d->present) {
        printgc(12, 0, "format: %s not present", device);
        return;
    }
 
    DiskFsType fst = FS_TYPE_FAT32;
    if (strcmp(fsname, "fat16") == 0) fst = FS_TYPE_FAT16;
 
    printgc(14, 0, "FORMATTING %s as %s", device, fsname);
    printgc(12, 0, "Model: %s", d->model[0] ? d->model : "(no name)");
    printgc(12, 0, "Size : ~%u MB", d->lba_count / 2048);
    printgc(12, 0, "Type exactly 'YES' to confirm:");
 
    char confirm[16] = {0};
    int idx = 0;
    while (idx < 15) {
        char key = get_char();
        if (key == '\n' || key == '\r') break;
        if (key == '\b' && idx > 0) { idx--; continue; }
        if (isprint(key)) {
            confirm[idx++] = key;
            print_raw(key, 12, 0);
        }
    }
    print_raw('\n', 7, 0);
 
    if (strcmp(confirm, "YES") != 0) {
        printgc(10, 0, "Format cancelled.");
        return;
    }
 
    AtaDiskIO disk(drive);
    int ret = disk_format(&disk, d->lba_count, label, fst);
 
    if (ret == 0)
        printgc(10, 0, "Format completed. Label: %s", label);
    else
        printgc(12, 0, "Format failed (code %d)", ret);
}

static void sreboot(char* args, bool is_elevated)   {
    if (is_elevated) {
        shrboot = true;
        shell_running = false;
    } else {
        printgc(8, 0, "Access denied.");
    }
}

static void sshutdown(char* args, bool is_elevated) {
    if (is_elevated) {
        shsdown = true;
        shell_running = false;
    } else {
        printgc(8, 0, "Access denied.");
    }
}

void run_shell() {
    set_user("user");
    vfs_set_current_user(false);

    char buf[MAX_CMD_LEN];

    printgc(15, 0, "\nWelcome \nQMSH %s", SH_VER);
    printgc(7,  0, "Type 'help' for available commands\n");

    shell_running = true;

    while (shell_running) {
        print_prompt();

        int cursor = 0;
        memset(buf, 0, sizeof(buf));

        while (1) {
            char key = get_char();
            if (key == 0) continue;

            if (key == '\n') {
                buf[cursor] = '\0';
                print_raw('\n', 0x7, 0x0);
                break;
            }
            if (key == '\b' && cursor > 0) {
                cursor--;
                buf[cursor] = '\0';
                int cx = cursor_x - 1, cy = cursor_y;
                goto_xy(cx, cy);
                print_raw(' ', 0x7, 0x0);
                goto_xy(cx, cy);
            }
            else if (isprint(key) && cursor < MAX_CMD_LEN-1) {
                buf[cursor++] = key;
                buf[cursor] = '\0';
                print_raw(key, 0x7, 0x0);
            }
        }

        if (cursor > 0) {
            history_add(buf);
            execute_command(buf, is_root);
        }
    }
}
