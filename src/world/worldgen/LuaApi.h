#pragma once

// Minimal Lua 5.4 C-API declarations used by Omnigrid. Keeping this tiny
// compatibility header in-tree means the engine only needs the Lua runtime
// library at link/runtime; distro development headers are not required.
// The ABI types below match the stock Lua 5.4 build used by mainstream 64-bit
// Linux distributions (double numbers, 64-bit integers).

#include <cstddef>
#include <cstdint>

extern "C"
{
    struct lua_State;
    using lua_Number = double;
    using lua_Integer = std::int64_t;
    static_assert( sizeof( lua_Integer ) == 8,
                   "Omnigrid worldgen requires 64-bit Lua integers" );
    using lua_KContext = std::intptr_t;
    using lua_CFunction = int ( * )( lua_State * );
    using lua_KFunction = int ( * )( lua_State *, int, lua_KContext );

    lua_State *luaL_newstate();
    void luaL_openlibs( lua_State *L );
    int luaL_loadbufferx( lua_State *L, const char *buff, std::size_t sz,
                          const char *name, const char *mode );
    void lua_close( lua_State *L );

    int lua_getglobal( lua_State *L, const char *name );
    int lua_getfield( lua_State *L, int idx, const char *key );
    void lua_setfield( lua_State *L, int idx, const char *key );
    void lua_setglobal( lua_State *L, const char *name );
    int lua_type( lua_State *L, int idx );
    int lua_gettop( lua_State *L );
    void lua_settop( lua_State *L, int idx );

    void lua_pushnil( lua_State *L );
    void lua_pushnumber( lua_State *L, lua_Number n );
    void lua_pushinteger( lua_State *L, lua_Integer n );
    void lua_pushvalue( lua_State *L, int idx );
    void lua_pushcclosure( lua_State *L, lua_CFunction fn, int n );

    lua_Number lua_tonumberx( lua_State *L, int idx, int *isnum );
    lua_Integer lua_tointegerx( lua_State *L, int idx, int *isnum );
    const char *lua_tolstring( lua_State *L, int idx, std::size_t *len );

    int lua_pcallk( lua_State *L, int nargs, int nresults, int errfunc,
                    lua_KContext ctx, lua_KFunction k );
}

namespace worldgen::lua54
{
    inline constexpr int OK = 0;
    inline constexpr int TTABLE = 5;
    inline constexpr int TFUNCTION = 6;

    inline void pop( lua_State *L, int count )
    {
        lua_settop( L, -count - 1 );
    }

    inline int pcall( lua_State *L, int nargs, int nresults )
    {
        return lua_pcallk( L, nargs, nresults, 0, 0, nullptr );
    }
} // namespace worldgen::lua54
