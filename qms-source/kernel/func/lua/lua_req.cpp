#include "lua_req.h"

extern "C" {
#include<luajit/src/lauxlib.h>
}
#include<io/keyboard.h>
#include<io/printg.h>
#include<func/lib/rmem/memory.h>
#include<bin/shell.h>
#include<func/vfs.h>

// -------------
// for req__func
// -------------

static int lua_kernel_execute_command(lua_State* state){
    size_t command_length;
    const char* command = luaL_checklstring(state, 1, &command_length);
    if (!lua_mem_manager || command_length == static_cast<size_t>(-1)) {
        return luaL_error(state, "unable to copy command");
    }

    char* mutable_command = static_cast<char*>(lua_mem_manager->allocate(command_length + 1));
    if (!mutable_command) {
        return luaL_error(state, "unable to allocate command buffer");
    }

    for (size_t i = 0; i < command_length; ++i) {
        mutable_command[i] = command[i];
    }
    mutable_command[command_length] = '\0';

    execute_command(mutable_command, false);
    lua_mem_manager->free(mutable_command);
    return 0;
}

static int lua_kernel_input(lua_State* state){
    if (!lua_mem_manager)
        return luaL_error(state, "input: Lua memory manager is unavailable");

    char* input = nullptr;
    size_t length = 0;
    size_t capacity = 0;

    for (;;) {
        char key = get_char();
        if (!key) continue;

        if (key == '\n' || key == '\r') {
            print_raw('\n', 7, 0);
            break;
        }

        if (key == '\b' || key == 127) {
            if (length > 0) {
                --length;
                cursor_back();
                print_raw(' ', 7, 0);
                cursor_back();
            }
            continue;
        }

        if (key >= 32 && key <= 126) {
            if (length == capacity) {
                size_t new_capacity = capacity ? capacity * 2 : 64;
                if (new_capacity < capacity) {
                    if (input) lua_mem_manager->free(input);
                    return luaL_error(state, "input: line is too long");
                }
                char* grown = static_cast<char*>(lua_mem_manager->reallocate(input, new_capacity));
                if (!grown) {
                    if (input) lua_mem_manager->free(input);
                    return luaL_error(state, "input: out of memory");
                }
                input = grown;
                capacity = new_capacity;
            }
            input[length++] = key;
            print_raw(key, 7, 0);
        }
    }

    lua_pushlstring(state, input ? input : "", length);
    if (input) lua_mem_manager->free(input);
    return 1;
}

static int lua_vfs_creat_file(lua_State* s){
    const char* path = lua_tolstring(s, 1, nullptr);
    VFS_Error err = vfs_creat(path, VFS_ENT_FILE);

    if(!err==VFS_OK)return luaL_error(s, vfs_strerror(err));
    else lua_pushboolean(s, 1);return 1;
} // vfs.makefile(path+name)


static int lua_vfs_creat_dir(lua_State* s){
    const char* path = lua_tolstring(s, 1, nullptr);
    VFS_Error err = vfs_creat(path, VFS_ENT_DIR);

    if(!err==VFS_OK)return luaL_error(s, vfs_strerror(err));
    else lua_pushboolean(s, 1);return 1;
} // vfs.makedir(path+name)

static int lua_vfs_read_file(lua_State* s){
    size_t path_length;
    const char* path = luaL_checklstring(s, 1, &path_length);
    if (path_length == 0 || path_length >= VFS_MAX_PATH)
        return luaL_error(s, "vfs.readfile: invalid path length");
    for (size_t i = 0; i < path_length; ++i) {
        if (path[i] == '\0')
            return luaL_error(s, "vfs.readfile: path contains a null byte");
    }

    VFS_Stat stat;
    VFS_Error err = vfs_stat_path(path, &stat);
    if (err != VFS_OK)
        return luaL_error(s, "vfs.readfile: %s", vfs_strerror(err));
    if (stat.type != VFS_ENT_FILE)
        return luaL_error(s, "vfs.readfile: %s", vfs_strerror(VFS_ERR_NOT_FILE));
    if (!lua_mem_manager)
        return luaL_error(s, "vfs.readfile: Lua memory manager is unavailable");

    size_t buffer_size = stat.size ? stat.size : 1;
    char* buffer = static_cast<char*>(lua_mem_manager->allocate(buffer_size));
    if (!buffer)
        return luaL_error(s, "vfs.readfile: out of memory");

    uint32_t content_size = stat.size;
    err = vfs_read_file(path, buffer, &content_size);
    if (err != VFS_OK) {
        lua_mem_manager->free(buffer);
        return luaL_error(s, "vfs.readfile: %s", vfs_strerror(err));
    }

    lua_pushlstring(s, buffer, content_size);
    lua_mem_manager->free(buffer);
    return 1;
}

static int lua_vfs_remove_file(lua_State* s){
    size_t path_length;
    const char* path = luaL_checklstring(s, 1, &path_length);
    if (path_length == 0 || path_length >= VFS_MAX_PATH)
        return luaL_error(s, "vfs.removefile: invalid path length");
    for (size_t i = 0; i < path_length; ++i) {
        if (path[i] == '\0')
            return luaL_error(s, "vfs.removefile: path contains a null byte");
    }

    VFS_Error err = vfs_unlink(path);
    if (err != VFS_OK)
        return luaL_error(s, "vfs.removefile: %s", vfs_strerror(err));

    lua_pushboolean(s, 1);
    return 1;
}

static int lua_vfs_remove_dir(lua_State* s){
    size_t path_length;
    const char* path = luaL_checklstring(s, 1, &path_length);
    if (path_length == 0 || path_length >= VFS_MAX_PATH)
        return luaL_error(s, "vfs.removedir: invalid path length");
    for (size_t i = 0; i < path_length; ++i) {
        if (path[i] == '\0')
            return luaL_error(s, "vfs.removedir: path contains a null byte");
    }

    VFS_Error err = vfs_rmdir(path);
    if (err != VFS_OK)
        return luaL_error(s, "vfs.removedir: %s", vfs_strerror(err));

    lua_pushboolean(s, 1);
    return 1;
}

static int lua_vfs_write_file(lua_State* s){
    size_t path_length;
    const char* path = luaL_checklstring(s, 1, &path_length);
    if (path_length == 0 || path_length >= VFS_MAX_PATH)
        return luaL_error(s, "vfs.writefile: invalid path length");
    for (size_t i = 0; i < path_length; ++i) {
        if (path[i] == '\0')
            return luaL_error(s, "vfs.writefile: path contains a null byte");
    }

    size_t content_length;
    const char* contents = luaL_checklstring(s, 2, &content_length);
    VFS_Stat stat;
    VFS_Error err = vfs_stat_path(path, &stat);
    if (err != VFS_OK)
        return luaL_error(s, "vfs.writefile: %s", vfs_strerror(err));
    if (stat.type != VFS_ENT_FILE)
        return luaL_error(s, "vfs.writefile: %s", vfs_strerror(VFS_ERR_NOT_FILE));

    err = vfs_write_file(path, contents, static_cast<uint32_t>(content_length));
    if (err != VFS_OK)
        return luaL_error(s, "vfs.writefile: %s", vfs_strerror(err));

    lua_pushboolean(s, 1);
    return 1;
}

static int lua_vfs_get_cwd(lua_State* s){
    const char* cwd = vfs_getcwd();
    lua_pushstring(s, cwd);
    return 1;
}

static int lua_advancedio_log_error(lua_State* s){
    size_t msg_length;
    const char* msg = luaL_checklstring(s, 1, &msg_length);
    if (msg_length == 0 || msg_length >= 1024)
        return luaL_error(s, "advancedio.log_error: invalid message length");
    for (size_t i = 0; i < msg_length; ++i) {
        if (msg[i] == '\0')
            return luaL_error(s, "advancedio.log_error: message contains a null byte");
    }

    log(1, msg);
    return 0;
}

static int lua_advancedio_log_warn(lua_State* s){
    size_t msg_length;
    const char* msg = luaL_checklstring(s, 1, &msg_length);
    if (msg_length == 0 || msg_length >= 1024)
        return luaL_error(s, "advancedio.log_warn: invalid message length");
    for (size_t i = 0; i < msg_length; ++i) {
        if (msg[i] == '\0')
            return luaL_error(s, "advancedio.log_warn: message contains a null byte");
    }

    log(2, msg);
    return 0;
}

static int lua_advancedio_log_info(lua_State* s){
    size_t msg_length;
    const char* msg = luaL_checklstring(s, 1, &msg_length);
    if (msg_length == 0 || msg_length >= 1024)
        return luaL_error(s, "advancedio.log_info: invalid message length");
    for (size_t i = 0; i < msg_length; ++i) {
        if (msg[i] == '\0')
            return luaL_error(s, "advancedio.log_info: message contains a null byte");
    }

    log(3, msg);
    return 0;
}

static int lua_advancedio_log_debug(lua_State* s){
    size_t msg_length;
    const char* msg = luaL_checklstring(s, 1, &msg_length);
    if (msg_length == 0 || msg_length >= 1024)
        return luaL_error(s, "advancedio.log_debug: invalid message length");
    for (size_t i = 0; i < msg_length; ++i) {
        if (msg[i] == '\0')
            return luaL_error(s, "advancedio.log_debug: message contains a null byte");
    }

    log(4, msg);
    return 0;
}

static int lua_advancedio_printgc(lua_State* s){
    size_t msg_length;
    const char* msg = luaL_checklstring(s, 1, &msg_length);
    int t = static_cast<int>(luaL_checkinteger(s, 2));
    int b = static_cast<int>(luaL_checkinteger(s, 3));

    if (msg_length == 0 || msg_length >= 1024)
        return luaL_error(s, "advancedio.printgc: invalid message length");
    for (size_t i = 0; i < msg_length; ++i) {
        if (msg[i] == '\0')
            return luaL_error(s, "advancedio.printgc: message contains a null byte");
    }

    bool no_newline = static_cast<uint8_t>(msg[msg_length - 1]) == 0x01;
    if (no_newline) --msg_length;

    char printable_msg[1024];
    for (size_t i = 0; i < msg_length; ++i)
        printable_msg[i] = msg[i];
    printable_msg[msg_length] = '\0';

    if (no_newline)
        printgc(t, b, "%s" PG_NNL, printable_msg);
    else
        printgc(t, b, "%s", printable_msg);
    return 0;
}

static int lua_advancedio_color_value(lua_State* s){
    lua_pushinteger(s, lua_tointeger(s, lua_upvalueindex(1)));
    return 1;
}

static void lua_advancedio_add_color(lua_State* s, const char* name, int value){
    lua_pushinteger(s, value);
    lua_pushcclosure(s, lua_advancedio_color_value, 1);
    lua_setfield(s, -2, name);
}

static int ____callnull(lua_State*){return 0;} // служебная функция нихуя неделанья

// ---------
// req__func
// ---------

void req__shell(lua_State* S){
    lua_newtable(S);

    lua_pushcfunction(S, lua_kernel_execute_command);
    lua_setfield(S, -2, "exec");

    lua_setglobal(S, "shell");
}

void req__easy_input(lua_State* S){
    lua_newtable(S);

    lua_pushcfunction(S, lua_kernel_input);
    lua_setfield(S, -2, "input");

    lua_pushcfunction(S, ____callnull);
    lua_setfield(S, -2, "finput"); // как input но не должен замораживать луа машину

    lua_setglobal(S, "easyinput");
}

void req__vfs(lua_State* s){
    lua_newtable(s);

    lua_pushcfunction(s, lua_vfs_creat_file);
    lua_setfield(s, -2, "makefile");

    lua_pushcfunction(s, lua_vfs_creat_dir);
    lua_setfield(s, -2, "makedir");

    lua_pushcfunction(s, lua_vfs_read_file);
    lua_setfield(s, -2, "readfile");

    lua_pushcfunction(s, lua_vfs_remove_file);
    lua_setfield(s, -2, "removefile");

    lua_pushcfunction(s, lua_vfs_remove_dir);
    lua_setfield(s, -2, "removedir");

    lua_pushcfunction(s, lua_vfs_write_file);
    lua_setfield(s, -2, "writefile");

    lua_pushcfunction(s, lua_vfs_get_cwd);
    lua_setfield(s, -2, "getcwd");

    lua_setglobal(s, "vfs");
}

void req__advancedio(lua_State* s){
    lua_newtable(s);

    lua_pushcfunction(s, lua_advancedio_log_error);
    lua_setfield(s, -2, "log_error");

    lua_pushcfunction(s, lua_advancedio_log_warn);
    lua_setfield(s, -2, "log_warn");

    lua_pushcfunction(s, lua_advancedio_log_info);
    lua_setfield(s, -2, "log_info");

    lua_pushcfunction(s, lua_advancedio_log_debug);
    lua_setfield(s, -2, "log_debug");

    lua_pushcfunction(s, lua_advancedio_printgc);
    lua_setfield(s, -2, "printgc");

    lua_newtable(s);
    lua_advancedio_add_color(s, "black", 0);
    lua_advancedio_add_color(s, "blue", 1);
    lua_advancedio_add_color(s, "green", 2);
    lua_advancedio_add_color(s, "cyan", 3);
    lua_advancedio_add_color(s, "red", 4);
    lua_advancedio_add_color(s, "magenta", 5);
    lua_advancedio_add_color(s, "brown", 6);
    lua_advancedio_add_color(s, "light_gray", 7);
    lua_advancedio_add_color(s, "dark_gray", 8);
    lua_advancedio_add_color(s, "bright_blue", 9);
    lua_advancedio_add_color(s, "bright_green", 10);
    lua_advancedio_add_color(s, "bright_cyan", 11);
    lua_advancedio_add_color(s, "bright_red", 12);
    lua_advancedio_add_color(s, "bright_magenta", 13);
    lua_advancedio_add_color(s, "yellow", 14);
    lua_advancedio_add_color(s, "white", 15);
    lua_setfield(s, -2, "colors");

    lua_setglobal(s, "advancedio");
}