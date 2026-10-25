#include <func/lib/rmem/memory.h>
#include <func/lua/lua_runtime.h>
#include <func/io/printg.h>
#include <func/lib/math/math.h>
#include <func/lua/lua_easy.h>
#include <func/io/serial.h>

extern "C" {
#include <luajit/src/lua.h>
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
    // таблица serial
    lua_newtable(LS);
    // функция serial.println
    lua_pushcfunction(LS, serial_println_serial);
    lua_setfield(LS, -2, "println");
    // устaновка таблицы serial
    lua_setglobal(LS, "serial");

    LuaSource source = {source_code, false};
    int status = lua_load(LS, lua_source_reader, &source, "=boot.lua");
    if (status == 0) status = lua_pcall(LS, 0, 0, 0);
    if (status != 0) {
        const char* message = lua_tolstring(LS, -1, nullptr);
        printg("Lua error: %s", message ? message : "unknown error");
    }

    lua_close(LS);
}
