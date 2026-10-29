#include <func/lib/rmem/memory.h>
#include <func/lua/lua_runtime.h>
#include <func/io/printg.h>
#include <func/lib/math/math.h>
#include <func/lib/strtools/string.h>
#include <func/lua/lua_easy.h>
#include <func/io/serial.h>
#include <func/lib/metad.h>
#include <bin/shell.h>
#include "lua_req.h"

extern "C" {
#include <luajit/src/lua.h>
#include <luajit/src/lauxlib.h>
#include <luajit/src/lualib.h>
}

extern "C" void printg(const char* fmt, ...);

[[noreturn]] static void lua_runtime_halt(const char* message) {
    printg("%s", message);
    for (;;) __asm__ __volatile__("hlt");
}

extern "C" [[noreturn]] void exit(int status) {
    (void)status;
    lua_runtime_halt("LuaJIT attempted to exit the kernel");
}

static double lua_math_unavailable(const char* name) {
    printg("LuaJIT math function unavailable: %s", name);
    return 0.0;
}

extern "C" double ldexp(double, int) { lua_math_unavailable("ldexp"); }
extern "C" double pow(double, double) { lua_math_unavailable("pow"); }
extern "C" double fabs(double value) { return value < 0.0 ? -value : value; }
extern "C" double log2(double) { lua_math_unavailable("log2"); }
extern "C" double log10(double) { lua_math_unavailable("log10"); }
extern "C" double exp(double) { lua_math_unavailable("exp"); }
extern "C" double sin(double) { lua_math_unavailable("sin"); }
extern "C" double cos(double) { lua_math_unavailable("cos"); }
extern "C" double tan(double) { lua_math_unavailable("tan"); }
extern "C" double asin(double) { lua_math_unavailable("asin"); }
extern "C" double acos(double) { lua_math_unavailable("acos"); }
extern "C" double atan(double value) { return atan2d(value, 1.0); }
extern "C" double sinh(double) { lua_math_unavailable("sinh"); }
extern "C" double cosh(double) { lua_math_unavailable("cosh"); }
extern "C" double tanh(double) { lua_math_unavailable("tanh"); }
extern "C" double atan2(double y, double x) { return atan2d(y, x); }
extern "C" double fmod(double x, double y) { return fmodd(x, y); }

static void* lua_kernel_alloc(void* ud, void* ptr, size_t, size_t new_size) {
    MemoryManager* mem  = (MemoryManager*)ud;
    if (!ud) return nullptr;

    if (new_size == 0) {
        mem->free(ptr);
        return nullptr;
    }
    if (!ptr) return mem->allocate(new_size);
    return mem->reallocate(ptr, new_size);
}

static int lua_kernel_print(lua_State* state) {
    const char* text = lua_tolstring(state, 1, nullptr);
    printg("%s", text ? text : "");
    return 0;
}

static int lua_kernel_func_clear(lua_State*) {
    clear_screen();
    return 0;
}

static int lua_kernel_panic(lua_State* state) {
    const char* message = lua_tolstring(state, -1, nullptr);
    printg("Lua panic: %s", message ? message : "unknown error");
    return 0;
}

static int serial_println_serial(lua_State* state) {
    const char* text = lua_tolstring(state, 1, nullptr);
    serial_println(text);
    return 0;
}

static int lua_serial_new_line_manual(lua_State*){
    serial_new_line_manual();
    return 0;
}

static int lua_kernel_ver_get(lua_State* state){
    lua_pushstring(state, FERR_VER);
    return 1;
}

static int lua_qms_kernel_ver_get(lua_State* state){
    lua_pushstring(state, CORE_VER);
    return 1;
}

struct LuaGlobalValue {
    LuaGlobalValue* next;
    char* name;
    size_t name_length;
    int type;
    size_t string_length;
    union {
        lua_Number number;
        int boolean;
        char* string;
    } value;
};

static LuaGlobalValue* lua_global_values;

static LuaGlobalValue* lua_global_find(const char* name, size_t name_length) {
    for (LuaGlobalValue* entry = lua_global_values; entry; entry = entry->next) {
        if (entry->name_length == name_length &&
            memcmp(entry->name, name, name_length) == 0) {
            return entry;
        }
    }
    return nullptr;
}

static void lua_global_free_value(LuaGlobalValue* entry) {
    if (entry->type == LUA_TSTRING) {
        lua_mem_manager->free(entry->value.string);
    }
}

static void lua_global_remove(LuaGlobalValue* entry) {
    LuaGlobalValue** link = &lua_global_values;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next;
    lua_global_free_value(entry);
    lua_mem_manager->free(entry->name);
    lua_mem_manager->free(entry);
}

static int lua_global_set(lua_State* state) {
    size_t name_length;
    const char* name = luaL_checklstring(state, 1, &name_length);
    LuaGlobalValue* entry = lua_global_find(name, name_length);
    const int type = lua_type(state, 2);

    if (type == LUA_TNIL) {
        if (entry && lua_mem_manager) lua_global_remove(entry);
        return 0;
    }

    if (type != LUA_TNUMBER && type != LUA_TSTRING && type != LUA_TBOOLEAN) {
        return luaL_error(state, "glob_set supports only numbers, strings, booleans, and nil");
    }
    if (!lua_mem_manager) {
        return luaL_error(state, "global variable storage is unavailable");
    }

    char* string_copy = nullptr;
    size_t string_length = 0;
    if (type == LUA_TSTRING) {
        size_t length;
        const char* string = lua_tolstring(state, 2, &length);
        if (length == static_cast<size_t>(-1)) {
            return luaL_error(state, "global string is too large");
        }
        string_copy = static_cast<char*>(lua_mem_manager->allocate(length ? length : 1));
        if (!string_copy) {
            return luaL_error(state, "unable to allocate global string");
        }
        if (length) memcpy(string_copy, string, length);
        string_length = length;
    }

    if (!entry) {
        if (name_length == static_cast<size_t>(-1)) {
            if (string_copy) lua_mem_manager->free(string_copy);
            return luaL_error(state, "global variable name is too large");
        }
        entry = static_cast<LuaGlobalValue*>(lua_mem_manager->allocate(sizeof(LuaGlobalValue)));
        char* name_copy = static_cast<char*>(lua_mem_manager->allocate(name_length + 1));
        if (!entry || !name_copy) {
            if (entry) lua_mem_manager->free(entry);
            if (name_copy) lua_mem_manager->free(name_copy);
            if (string_copy) lua_mem_manager->free(string_copy);
            return luaL_error(state, "unable to allocate global variable");
        }
        memcpy(name_copy, name, name_length);
        name_copy[name_length] = '\0';
        entry->name = name_copy;
        entry->name_length = name_length;
        entry->next = lua_global_values;
        lua_global_values = entry;
    } else {
        lua_global_free_value(entry);
    }

    entry->type = type;
    entry->string_length = string_length;
    if (type == LUA_TNUMBER) {
        entry->value.number = lua_tonumber(state, 2);
    } else if (type == LUA_TBOOLEAN) {
        entry->value.boolean = lua_toboolean(state, 2);
    } else {
        entry->value.string = string_copy;
    }
    return 0;
}

static int lua_global_get(lua_State* state) {
    size_t name_length;
    const char* name = luaL_checklstring(state, 1, &name_length);
    LuaGlobalValue* entry = lua_global_find(name, name_length);
    if (!entry) {
        lua_pushnil(state);
    } else if (entry->type == LUA_TNUMBER) {
        lua_pushnumber(state, entry->value.number);
    } else if (entry->type == LUA_TBOOLEAN) {
        lua_pushboolean(state, entry->value.boolean);
    } else {
        lua_pushlstring(state, entry->value.string, entry->string_length);
    }
    return 1;
}

static int lua_global_free(lua_State* state) {
    size_t name_length;
    const char* name = luaL_checklstring(state, 1, &name_length);
    LuaGlobalValue* entry = lua_global_find(name, name_length);
    if (entry) {
        if (!lua_mem_manager) {
            return luaL_error(state, "global variable storage is unavailable");
        }
        lua_global_remove(entry);
    }
    return 0;
}

static int lua_require(lua_State* state){
    const char* req = lua_tolstring(state, 1, nullptr);

    if(req && strcmp(req, "shell") == 0){
        req__shell(state);
        return 0;
    }else if (req && strcmp(req, "easyinput") == 0){
        req__easy_input(state);
        return 0;
    }else{
        lua_pushstring(state, "invalid kernel module name");
        return 1;
    }
}

static int lua_printinol(lua_State* state){
    const char* req = lua_tolstring(state, 1, nullptr);
    printg("%s" PG_NNL, req);
    return 0;
}

struct LuaSource {
    const char* text;
    bool returned;
};

static const char* lua_source_reader(lua_State*, void* data, size_t* size) {
    LuaSource* source = static_cast<LuaSource*>(data);
    if (source->returned) {
        *size = 0;
        return nullptr;
    }

    source->returned = true;
    const char* text = source->text;
    size_t length = 0;
    while (text[length]) ++length;
    *size = length;
    return text;
}

void lua_run_luac(char* source_code) {
    lua_State* LS = lua_newstate(lua_kernel_alloc, lua_mem_manager);
    if (!LS) {
        printg("Lua: failed to create state");
        return;
    }

    luaopen_base(LS);
    luaopen_math(LS);

    lua_atpanic(LS, lua_kernel_panic);

    // функция print
    insert_cclosure_func(LS, &lua_kernel_print, "print");
    // функция clear
    insert_cclosure_func(LS, &lua_kernel_func_clear, "clear");
    // функция printi ввод в одну строку
    insert_cclosure_func(LS, &lua_printinol, "printi");


    // таблица serial
    lua_newtable(LS);
    // функция serial.println
    lua_pushcfunction(LS, serial_println_serial);
    lua_setfield(LS, -2, "println");
    // функция serial.newline_m
    lua_pushcfunction(LS, lua_serial_new_line_manual);
    lua_setfield(LS, -2, "newline_m");
    // устaновка таблицы serial
    lua_setglobal(LS, "serial");

    // таблица kernel
    lua_newtable(LS);
    // функция kernel.version
    lua_pushcfunction(LS, lua_kernel_ver_get);
    lua_setfield(LS,-2,"version");
    // функция kernel.qms_ver
    lua_pushcfunction(LS, lua_qms_kernel_ver_get);
    lua_setfield(LS,-2,"qms_ver");
    // установка таблицы kernel
    lua_setglobal(LS, "kernel");

    insert_cclosure_func(LS, lua_require, "k_req");
    insert_cclosure_func(LS, lua_global_set, "glob_set");
    insert_cclosure_func(LS, lua_global_get, "glob_get");
    insert_cclosure_func(LS, lua_global_free, "glob_free");

    LuaSource source = {source_code, false};
    int status = lua_load(LS, lua_source_reader, &source, "=boot.lua");
    if (status == 0) status = lua_pcall(LS, 0, 0, 0);
    if (status != 0) {
        const char* message = lua_tolstring(LS, -1, nullptr);
        printg("Lua error: %s", message ? message : "unknown error");
    }

    lua_close(LS);
}
