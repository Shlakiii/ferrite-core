#ifndef LUA_EASY_H
#define LUA_EASY_H

extern "C" {
#include <luajit/src/lua.h>
}

void insert_cclosure_func(lua_State* state, lua_CFunction c_function, const char* newfunc_name);

#endif