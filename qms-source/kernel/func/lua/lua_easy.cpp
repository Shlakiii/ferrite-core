#include <func/lua/lua_easy.h>

void insert_cclosure_func(lua_State* state, lua_CFunction c_function, const char* newfunc_name) {
    lua_pushcfunction(state, c_function);
    lua_setglobal(state, newfunc_name);
}