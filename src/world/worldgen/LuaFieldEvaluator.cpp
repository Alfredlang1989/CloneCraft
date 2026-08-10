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
        ThreadVm &threadVmFor( std::uint64_t instanceId, const FieldConfig &config,
                               std::uint64_t worldSeed, const std::string &source )
        {
            thread_local std::unordered_map<std::uint64_t, std::unique_ptr<ThreadVm>> cache;
            auto it = cache.find( instanceId );
            if( it == cache.end() )
            {
                auto vm = std::make_unique<ThreadVm>();
                vm->state = createState( config, worldSeed, source, vm->context );
                it = cache.emplace( instanceId, std::move( vm ) ).first;
            }
            return *it->second;
        }

        int pinFieldFunction( lua_State *L, const FieldConfig &config )
        {
            if( lua_getglobal( L, config.functionName.c_str() ) != lua54::TFUNCTION )
                throw std::runtime_error( config.scriptPath.string() + ": field function vanished" );
            return lua_gettop( L );
        }

        double invokeWorldField( lua_State *L, int functionIndex, const FieldConfig &config,
                                 std::uint64_t seed )
        {
            lua_pushvalue( L, functionIndex );
            lua_pushinteger( L, static_cast<lua_Integer>( seed & 0x7FFFFFFFFFFFFFFFULL ) );
            if( lua54::pcall( L, 1, 1 ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( L ) );
            int ok = 0;
            const double result = lua_tonumberx( L, -1, &ok );
            lua54::pop( L, 1 );
            if( !ok || !std::isfinite( result ) )
                throw std::runtime_error( config.scriptPath.string() + ": field function must return a finite number" );
            return result;
        }

        double invokeStructure( lua_State *L, int functionIndex, const FieldConfig &config,
                                std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z )
        {
            lua_pushvalue( L, functionIndex );
            lua_pushinteger( L, static_cast<lua_Integer>( x ) );
            lua_pushinteger( L, static_cast<lua_Integer>( y ) );
            lua_pushinteger( L, static_cast<lua_Integer>( z ) );
            lua_pushinteger( L, static_cast<lua_Integer>( seed & 0x7FFFFFFFFFFFFFFFULL ) );
            if( lua54::pcall( L, 4, 1 ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( L ) );
            int ok = 0;
            const double result = lua_tonumberx( L, -1, &ok );
            lua54::pop( L, 1 );
            if( !ok || !std::isfinite( result ) )
                throw std::runtime_error( config.scriptPath.string() + ": structure function must return a finite number" );
            return result;
        }
    } // namespace

    double SampledField::at2D( std::int64_t lx, std::int64_t lz ) const
    {
        if( dimension != FieldDimension::D2 ) throw std::logic_error( "2D lookup on 3D field '" + id + "'" );
        return values[static_cast<std::size_t>( lx * EDGE + lz )];
    }
    double SampledField::at3D( std::int64_t lx, std::int64_t ly, std::int64_t lz ) const
    {
        if( dimension != FieldDimension::D3 ) throw std::logic_error( "3D lookup on 2D field '" + id + "'" );
        return values[static_cast<std::size_t>( ( lx * EDGE + ly ) * EDGE + lz )];
    }

    LuaFieldEvaluator::LuaFieldEvaluator( const FieldConfig &config, std::uint64_t worldSeed )
        : mConfig( config ), mWorldSeed( worldSeed ),
          mInstanceId( gNextEvaluatorId.fetch_add( 1u, std::memory_order_relaxed ) )
    {
        std::ifstream file( mConfig.scriptPath, std::ios::binary );
        if( !file ) throw std::runtime_error( "could not open worldgen Lua field '" + mConfig.scriptPath.string() + "'" );
        std::ostringstream buffer; buffer << file.rdbuf(); mScriptSource = buffer.str();
        LuaContext context;
        (void)createState( mConfig, mWorldSeed, mScriptSource, context );
    }

    void LuaFieldEvaluator::sampleChunkXSlice( const world::ChunkAddress &chunk,
                                                   std::int64_t lx,
                                                   SampledField &sampled ) const
    {
        if( lx < 0 || lx >= EDGE )
            throw std::out_of_range( "worldgen X slice outside chunk" );
        if( sampled.dimension != mConfig.dimension || sampled.id != mConfig.id )
            throw std::invalid_argument( "worldgen sampled field does not match evaluator" );
        const std::size_t expected = mConfig.dimension == FieldDimension::D2
            ? static_cast<std::size_t>( EDGE * EDGE )
            : static_cast<std::size_t>( world::chunkVolume() );
        if( sampled.values.size() != expected )
            throw std::invalid_argument( "worldgen sampled field has wrong storage size" );

        ThreadVm &vm = threadVmFor( mInstanceId, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        lua_State *state = vm.state.get();
        const std::uint64_t scriptSeed = deriveNoiseSeed( mWorldSeed, mConfig.salt );
        const int functionIndex = pinFieldFunction( state, mConfig );
        vm.context.noise->beginChunkXSlice( chunk, lx );
        vm.context.hasSample = true;

        if( mConfig.dimension == FieldDimension::D2 )
        {
            for( std::int64_t lz = 0; lz < EDGE; ++lz )
            {
                vm.context.noise->setSliceSample( 0, lz );
                sampled.values[static_cast<std::size_t>( lx * EDGE + lz )] =
                    invokeWorldField( state, functionIndex, mConfig, scriptSeed );
            }
        }
        else
        {
            // Z before Y lets the mapper reuse the precomputed horizontal
            // macro warp for all 16 vertical samples in a column.
            for( std::int64_t lz = 0; lz < EDGE; ++lz )
                for( std::int64_t ly = 0; ly < EDGE; ++ly )
                {
                    vm.context.noise->setSliceSample( ly, lz );
                    sampled.values[static_cast<std::size_t>( ( lx * EDGE + ly ) * EDGE + lz )] =
                        invokeWorldField( state, functionIndex, mConfig, scriptSeed );
                }
        }
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
    }

    SampledField LuaFieldEvaluator::sampleChunk( const world::ChunkAddress &chunk ) const
    {
        SampledField sampled{ mConfig.id, mConfig.dimension, {} };
        sampled.values.resize( mConfig.dimension == FieldDimension::D2
            ? static_cast<std::size_t>( EDGE * EDGE )
            : static_cast<std::size_t>( world::chunkVolume() ) );
        for( std::int64_t lx = 0; lx < EDGE; ++lx )
            sampleChunkXSlice( chunk, lx, sampled );
        return sampled;
    }

    double LuaFieldEvaluator::sample2D( const world::BlockAddress &point ) const
    {
        if( mConfig.dimension != FieldDimension::D2 ) throw std::logic_error( "2D sample on non-2D field" );
        ThreadVm &vm = threadVmFor( mInstanceId, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.sample = point; vm.context.noise->setSample( point ); vm.context.hasSample = true;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeWorldField( state, functionIndex, mConfig,
                                                deriveNoiseSeed( mWorldSeed, mConfig.salt ) );
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
        return result;
    }

    double LuaFieldEvaluator::sample3D( const world::BlockAddress &point ) const
    {
        if( mConfig.dimension != FieldDimension::D3 ) throw std::logic_error( "3D sample on non-3D field" );
        ThreadVm &vm = threadVmFor( mInstanceId, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.sample = point; vm.context.noise->setSample( point ); vm.context.hasSample = true;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeWorldField( state, functionIndex, mConfig,
                                                deriveNoiseSeed( mWorldSeed, mConfig.salt ) );
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
        return result;
    }

    double LuaFieldEvaluator::sample3DWithSeed( std::int64_t x, std::int64_t y, std::int64_t z,
                                                 std::uint64_t sampleSeed ) const
    {
        ThreadVm &vm = threadVmFor( mInstanceId, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.hasSample = false;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeStructure( state, functionIndex, mConfig, sampleSeed, x, y, z );
        lua54::pop( state, 1 );
        return result;
    }
} // namespace worldgen
