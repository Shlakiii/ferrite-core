#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H

typedef struct lua_State lua_State;

extern "C" {
    lua_State* lua_create_state();
    void lua_run_luac_manual(lua_State* state, const char* script);
    void lua_run_luac(char* script);
}


#endif // LUA_RUNTIME_H