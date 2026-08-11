#include "world/worldgen/LuaFieldEvaluator.h"

#include "world/worldgen/MappedOpenSimplexNoise.h"
#include "world/worldgen/LuaApi.h"
#include "world/worldgen/NoiseSeed.h"

#include <atomic>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace worldgen
{
    namespace
    {
        constexpr std::int64_t EDGE = world::BLOCKS_PER_CHUNK_EDGE;
        std::atomic<std::uint64_t> gNextEvaluatorId{ 1u };

        struct LuaContext
        {
            std::uint64_t fieldSeed = 0;
            std::unique_ptr<MappedOpenSimplexNoise> noise;
            world::BlockAddress sample{};
            bool hasSample = false;
        };
        thread_local LuaContext *gLuaContext = nullptr;

        class LuaContextScope
        {
        public:
            explicit LuaContextScope( LuaContext &context ) : mPrevious( gLuaContext ) { gLuaContext = &context; }
            ~LuaContextScope() { gLuaContext = mPrevious; }
        private:
            LuaContext *mPrevious = nullptr;
        };

        std::int64_t integerFromLua( lua_State *L, int index, const char *what )
        {
            int ok = 0;
            const lua_Integer value = lua_tointegerx( L, index, &ok );
            if( !ok ) throw std::runtime_error( std::string( "worldgen Lua: " ) + what + " must be integer" );
            return static_cast<std::int64_t>( value );
        }
        double numberFromLua( lua_State *L, int index, double fallback = 0.0 )
        {
            int ok = 0;
            const double value = lua_tonumberx( L, index, &ok );
            return ok ? value : fallback;
        }
        std::uint64_t saltFromLua( lua_State *L, int index )
        {
            return static_cast<std::uint64_t>( integerFromLua( L, index, "noise salt" ) );
        }

        int luaNoise2( lua_State *L )
        {
            if( !gLuaContext || !gLuaContext->hasSample || !gLuaContext->noise )
                throw std::runtime_error( "noise2 used outside a world-field sample" );
            const double scale = numberFromLua( L, 1 );
            const std::uint64_t salt = saltFromLua( L, 2 );
            const int argc = lua_gettop( L );
            const double ox = argc >= 3 ? numberFromLua( L, 3 ) : 0.0;
            const double oz = argc >= 4 ? numberFromLua( L, 4 ) : 0.0;
            lua_pushnumber( L, gLuaContext->noise->noise2( scale, salt, ox, oz ) );
            return 1;
        }

        int luaNoise3( lua_State *L )
        {
            if( !gLuaContext || !gLuaContext->hasSample || !gLuaContext->noise )
                throw std::runtime_error( "noise3 used outside a world-field sample" );
            const double sx = numberFromLua( L, 1 );
            const double sy = numberFromLua( L, 2 );
            const double sz = numberFromLua( L, 3 );
            const std::uint64_t salt = saltFromLua( L, 4 );
            const int argc = lua_gettop( L );
            const double ox = argc >= 5 ? numberFromLua( L, 5 ) : 0.0;
            const double oy = argc >= 6 ? numberFromLua( L, 6 ) : 0.0;
            const double oz = argc >= 7 ? numberFromLua( L, 7 ) : 0.0;
            lua_pushnumber( L, gLuaContext->noise->noise3( sx, sy, sz, salt, ox, oy, oz ) );
            return 1;
        }

        struct LuaStateDeleter { void operator()( lua_State *s ) const { if( s ) lua_close( s ); } };
        using LuaStatePtr = std::unique_ptr<lua_State, LuaStateDeleter>;

        std::string luaError( lua_State *L )
        {
            std::size_t len = 0;
            const char *message = lua_tolstring( L, -1, &len );
            return message ? std::string( message, len ) : std::string( "unknown Lua error" );
        }

        LuaStatePtr createState( const FieldConfig &config, std::uint64_t worldSeed,
                                 const std::string &source, LuaContext &context )
        {
            LuaStatePtr state( luaL_newstate() );
            if( !state ) throw std::runtime_error( "worldgen Lua: luaL_newstate failed" );
            luaL_openlibs( state.get() );
            for( const char *name : { "io", "os", "package", "debug", "dofile", "loadfile", "require" } )
            {
                lua_pushnil( state.get() );
                lua_setglobal( state.get(), name );
            }
            if( lua_getglobal( state.get(), "math" ) == lua54::TTABLE )
            {
                lua_pushnil( state.get() ); lua_setfield( state.get(), -2, "random" );
                lua_pushnil( state.get() ); lua_setfield( state.get(), -2, "randomseed" );
            }
            lua54::pop( state.get(), 1 );

            context.fieldSeed = deriveNoiseSeed( worldSeed, config.salt );
            context.noise = std::make_unique<MappedOpenSimplexNoise>( worldSeed, context.fieldSeed );
            LuaContextScope scope( context );
            lua_pushcclosure( state.get(), luaNoise2, 0 ); lua_setglobal( state.get(), "noise2" );
            lua_pushcclosure( state.get(), luaNoise3, 0 ); lua_setglobal( state.get(), "noise3" );

            if( luaL_loadbufferx( state.get(), source.data(), source.size(),
                                  config.scriptPath.string().c_str(), "t" ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( state.get() ) );
            if( lua54::pcall( state.get(), 0, 0 ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( state.get() ) );
            if( lua_getglobal( state.get(), config.functionName.c_str() ) != lua54::TFUNCTION )
                throw std::runtime_error( config.scriptPath.string() + ": function '" + config.functionName + "' was not defined" );
            lua54::pop( state.get(), 1 );
            return state;
        }

        struct ThreadVm { LuaContext context; LuaStatePtr state; };
        struct CachedThreadVm
        {
            std::weak_ptr<const void> lifetime;
            std::unique_ptr<ThreadVm> vm;
        };

        using ThreadVmCache = std::unordered_map<std::uint64_t, CachedThreadVm>;

        ThreadVmCache &threadVmCache()
        {
            thread_local ThreadVmCache cache;
            return cache;
        }

        void pruneExpiredThreadVms( ThreadVmCache &cache )
        {
            for( auto it = cache.begin(); it != cache.end(); )
            {
                if( it->second.lifetime.expired() ) it = cache.erase( it );
                else ++it;
            }
        }

        ThreadVm &threadVmFor( std::uint64_t instanceId,
                               const std::shared_ptr<const void> &lifetime,
                               const FieldConfig &config, std::uint64_t worldSeed,
                               const std::string &source )
        {
            ThreadVmCache &cache = threadVmCache();
            // WorldGen instances may be reloaded many times on a long-lived
            // thread. Stale Lua states must not accumulate for the lifetime of
            // that thread. Worker-thread caches disappear when their jthreads
            // stop; long-lived caller threads prune here on their next sample.
            pruneExpiredThreadVms( cache );

            auto it = cache.find( instanceId );
            if( it == cache.end() )
            {
                auto vm = std::make_unique<ThreadVm>();
                vm->state = createState( config, worldSeed, source, vm->context );
                CachedThreadVm cached;
                cached.lifetime = lifetime;
                cached.vm = std::move( vm );
                it = cache.emplace( instanceId, std::move( cached ) ).first;
            }
            return *it->second.vm;
        }
