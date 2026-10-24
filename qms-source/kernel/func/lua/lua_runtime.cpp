#include <func/lib/rmem/memory.h>
#include <func/lua/lua_runtime.h>
#include <func/io/printg.h>
#include <func/lib/math/math.h>

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
    lua_runtime_halt("LuaJIT math is not enabled in the kernel");
}

//extern "C" double ldexp(double, int) { return lua_math_unavailable("ldexp"); }
//extern "C" double pow(double, double) { return lua_math_unavailable("pow"); }
//extern "C" double fabs(double) { return lua_math_unavailable("fabs"); }
//extern "C" double log2(double) { return lua_math_unavailable("log2"); }
//extern "C" double log10(double) { return lua_math_unavailable("log10"); }
//extern "C" double exp(double) { return lua_math_unavailable("exp"); }
//extern "C" double sin(double) { return lua_math_unavailable("sin"); }
//extern "C" double cos(double) { return lua_math_unavailable("cos"); }
//extern "C" double tan(double) { return lua_math_unavailable("tan"); }
//extern "C" double asin(double) { return lua_math_unavailable("asin"); }
//extern "C" double acos(double) { return lua_math_unavailable("acos"); }
//extern "C" double atan(double) { return lua_math_unavailable("atan"); }
//extern "C" double sinh(double) { return lua_math_unavailable("sinh"); }
//extern "C" double cosh(double) { return lua_math_unavailable("cosh"); }
//extern "C" double tanh(double) { return lua_math_unavailable("tanh"); }
//extern "C" double atan2(double, double) { return lua_math_unavailable("atan2"); }
extern "C" double fmod(double xx, double yy) { return fmodd(xx,yy); }

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

static int lua_kernel_panic(lua_State* state) {
    const char* message = lua_tolstring(state, -1, nullptr);
    printg("Lua panic: %s", message ? message : "unknown error");
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
    //luaopen_math(LS);

    lua_atpanic(LS, lua_kernel_panic);
    lua_pushcfunction(LS, lua_kernel_print);
    lua_setglobal(LS, "print");    

    LuaSource source = {source_code, false};
    int status = lua_load(LS, lua_source_reader, &source, "=boot.lua");
    if (status == 0) status = lua_pcall(LS, 0, 0, 0);
    if (status != 0) {
        const char* message = lua_tolstring(LS, -1, nullptr);
        printg("Lua error: %s", message ? message : "unknown error");
    }

    lua_close(LS);
}
