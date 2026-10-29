#include "lua_req.h"

extern "C" {
#include <luajit/src/lauxlib.h>
}
#include <io/keyboard.h>
#include <io/printg.h>
#include<func/lib/rmem/memory.h>
#include<bin/shell.h>

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