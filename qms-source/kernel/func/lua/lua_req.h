#ifndef LUA_REQ_H
#define LUA_REQ_H

#ifdef __cplusplus
extern "C" {
#endif
#include <luajit/src/lua.h>
#ifdef __cplusplus
}
#endif

void req__shell(lua_State* S);
void req__easy_input(lua_State* S);

#endif //LUA_REQ_H