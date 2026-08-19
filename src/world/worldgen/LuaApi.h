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
    using lua_Unsigned = unsigned long long;
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
    int lua_absindex( lua_State *L, int idx );
    void lua_rotate( lua_State *L, int idx, int n );

    void lua_pushnil( lua_State *L );
    void lua_pushnumber( lua_State *L, lua_Number n );
    void lua_pushinteger( lua_State *L, lua_Integer n );
    void lua_pushboolean( lua_State *L, int b );
    void lua_pushvalue( lua_State *L, int idx );
    void lua_pushcclosure( lua_State *L, lua_CFunction fn, int n );
    void lua_pushlightuserdata( lua_State *L, void *p );
    void lua_pushstring( lua_State *L, const char *s );
    void lua_pushlstring( lua_State *L, const char *s, std::size_t len );

    lua_Number lua_tonumberx( lua_State *L, int idx, int *isnum );
    lua_Integer lua_tointegerx( lua_State *L, int idx, int *isnum );
    const char *lua_tolstring( lua_State *L, int idx, std::size_t *len );
    int lua_toboolean( lua_State *L, int idx );
    void *lua_touserdata( lua_State *L, int idx );

    int lua_isinteger( lua_State *L, int idx );
    int lua_isnumber( lua_State *L, int idx );
    int lua_isstring( lua_State *L, int idx );
    int lua_isuserdata( lua_State *L, int idx );

    int lua_pcallk( lua_State *L, int nargs, int nresults, int errfunc,
                    lua_KContext ctx, lua_KFunction k );
    void lua_callk( lua_State *L, int nargs, int nresults, lua_KContext ctx,
                    lua_KFunction k );

    void lua_createtable( lua_State *L, int narr, int nrec );
    void lua_gettable( lua_State *L, int idx );
    void lua_settable( lua_State *L, int idx );
    void lua_rawget( lua_State *L, int idx );
    void lua_rawset( lua_State *L, int idx );
    void lua_rawgeti( lua_State *L, int idx, lua_Integer n );
    void lua_rawseti( lua_State *L, int idx, lua_Integer n );
    int lua_next( lua_State *L, int idx );
    lua_Unsigned lua_rawlen( lua_State *L, int idx );

    int lua_error( lua_State *L );
    int luaL_error( lua_State *L, const char *fmt, ... );

    int luaL_ref( lua_State *L, int t );
    void luaL_unref( lua_State *L, int t, int ref );

    const char *lua_setupvalue( lua_State *L, int funcindex, int n );
    const char *lua_getupvalue( lua_State *L, int funcindex, int n );

    int lua_getmetatable( lua_State *L, int idx );
    int lua_setmetatable( lua_State *L, int idx );

    struct lua_Debug;
    using lua_Hook = void ( * )( lua_State *, lua_Debug * );
    void lua_sethook( lua_State *L, lua_Hook func, int mask, int count );
}

/** Equivalent of the stock `lua_pop(L, n)` macro. */
inline void lua_pop( lua_State *L, int n )
{
    lua_settop( L, -n - 1 );
}

/** Equivalent of the stock `lua_remove(L, idx)` macro. */
inline void lua_remove( lua_State *L, int idx )
{
    lua_rotate( L, idx, -1 );
    lua_pop( L, 1 );
}

/** Equivalent of the stock `lua_pushcfunction(L, f)` macro. */
inline void lua_pushcfunction( lua_State *L, lua_CFunction f )
{
    lua_pushcclosure( L, f, 0 );
}

/** The stock type-predicate macros (not exported as functions). */
inline int lua_isnil( lua_State *L, int idx ) { return lua_type( L, idx ) == 0; }
inline int lua_isfunction( lua_State *L, int idx ) { return lua_type( L, idx ) == 6; }
inline int lua_istable( lua_State *L, int idx ) { return lua_type( L, idx ) == 5; }
inline int lua_isboolean( lua_State *L, int idx ) { return lua_type( L, idx ) == 1; }
inline int lua_islightuserdata( lua_State *L, int idx ) { return lua_type( L, idx ) == 2; }

namespace worldgen::lua54
{
    inline constexpr int OK = 0;
    inline constexpr int TTABLE = 5;
    inline constexpr int TFUNCTION = 6;

    inline constexpr int TNIL = 0;
    inline constexpr int TBOOLEAN = 1;
    inline constexpr int TLIGHTUSERDATA = 2;
    inline constexpr int TNUMBER = 3;
    inline constexpr int TSTRING = 4;
    inline constexpr int TUSERDATA = 7;
    inline constexpr int TTHREAD = 8;

    inline constexpr int NOREF = -2;
    inline constexpr int MULTRET = -1;
    // Lua 5.4: LUA_REGISTRYINDEX = -LUAI_MAXSTACK - 1000 = -1001000
    // (changed from the -10000 of older Lua versions).
    inline constexpr int REGISTRYINDEX = -1001000;
    // Hook mask bits: LUA_HOOKCALL=0, LUA_HOOKRET=1, LUA_HOOKLINE=2,
    // LUA_HOOKCOUNT=3 -> MASK* = (1 << HOOK*).
    inline constexpr int MASKCALL = ( 1 << 0 );
    inline constexpr int MASKRET = ( 1 << 1 );
    inline constexpr int MASKLINE = ( 1 << 2 );
    inline constexpr int MASKCOUNT = ( 1 << 3 );
    inline constexpr int luaUpvalueIndex( int i ) { return REGISTRYINDEX - i; }

    inline void pop( lua_State *L, int count )
    {
        lua_settop( L, -count - 1 );
    }

    inline int pcall( lua_State *L, int nargs, int nresults )
    {
        return lua_pcallk( L, nargs, nresults, 0, 0, nullptr );
    }
} // namespace worldgen::lua54
