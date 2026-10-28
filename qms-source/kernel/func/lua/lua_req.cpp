#ifdef __cplusplus
extern "C" {
#endif
#include <luajit/src/lua.h>
#include <luajit/src/lauxlib.h>
#include <luajit/src/lualib.h>
#ifdef __cplusplus
}
#endif
#include<func/lib/rmem/memory.h>
#include<bin/shell.h>

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

void req__shell(lua_State* S){
    lua_newtable(S);

    lua_pushcfunction(S, lua_kernel_execute_command);
    lua_setfield(S, -2, "exec");

    lua_setglobal(S, "shell");
}