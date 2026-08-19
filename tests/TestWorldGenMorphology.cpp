#include "TestHarness.h"

#include "world/coordinates/Coords.h"
#include "world/worldgen/LuaFieldEvaluator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <queue>
#include <utility>
#include <vector>

namespace
{
    worldgen::FieldConfig fieldConfig( const char *id, worldgen::FieldDimension dimension,
                                       const char *script, std::uint64_t salt )
    {
        worldgen::FieldConfig cfg;
        cfg.id = id;
        cfg.dimension = dimension;
        cfg.scriptPath = std::filesystem::path( OMNIGRID_DATA_DIR ) / "worldgen" / script;
        cfg.functionName = "sample";
        cfg.salt = salt;
        return cfg;
    }
}

TEST_CASE( default_river_field_remains_a_long_thin_filament )
{
    const auto cfg = fieldConfig( "river_mask", worldgen::FieldDimension::D2,
                                  "river_mask.lua", 1000u );
    worldgen::LuaFieldEvaluator river( cfg, 1337u );

    constexpr int N = 256;
    std::vector<std::uint8_t> mask( static_cast<std::size_t>( N * N ), 0u );
    const auto index = []( int x, int z ) {
        return static_cast<std::size_t>( x ) * static_cast<std::size_t>( N ) +
               static_cast<std::size_t>( z );
    };

    // The origin may now be a real ocean. Locate one deterministic land
    // filament first, then prove its local morphology at block resolution.
    bool foundLandChannel = false;
    std::int64_t centerX = 0, centerZ = 0;
    for( std::int64_t x = -65536; x <= 65536 && !foundLandChannel; x += 64 )
        for( std::int64_t z = -65536; z <= 65536; z += 64 )
        {
            if( river.sample2D( world::fromOriginOffset( x, 0, z ) ) < 0.024 )
            {
                centerX = x;
                centerZ = z;
                foundLandChannel = true;
                break;
            }
        }
    CHECK( foundLandChannel );

    std::size_t channelCount = 0u;
    for( int x = 0; x < N; ++x )
        for( int z = 0; z < N; ++z )
        {
            const world::BlockAddress p = world::fromOriginOffset(
                centerX + x - N / 2, 0, centerZ + z - N / 2 );
            if( river.sample2D( p ) < 0.024 )
            {
                mask[index( x, z )] = 1u;
                ++channelCount;
            }
        }

    CHECK( channelCount > static_cast<std::size_t>( N * N / 250 ) );
    CHECK( channelCount < static_cast<std::size_t>( N * N / 8 ) );

    std::vector<std::uint8_t> seen( mask.size(), 0u );
    int bestSpan = 0;
    for( int sx = 0; sx < N; ++sx )
        for( int sz = 0; sz < N; ++sz )
        {
            const std::size_t start = index( sx, sz );
            if( !mask[start] || seen[start] ) continue;

            std::queue<std::pair<int, int>> pending;
            pending.emplace( sx, sz );
            seen[start] = 1u;
            int minX = sx, maxX = sx, minZ = sz, maxZ = sz;
            while( !pending.empty() )
            {
                const auto [x, z] = pending.front();
                pending.pop();
                minX = std::min( minX, x ); maxX = std::max( maxX, x );
                minZ = std::min( minZ, z ); maxZ = std::max( maxZ, z );
                constexpr std::array<std::pair<int, int>, 4> dirs{
                    std::pair{ 1, 0 }, std::pair{ -1, 0 },
                    std::pair{ 0, 1 }, std::pair{ 0, -1 }
                };
                for( const auto &[dx, dz] : dirs )
                {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if( nx < 0 || nx >= N || nz < 0 || nz >= N ) continue;
                    const std::size_t ni = index( nx, nz );
                    if( mask[ni] && !seen[ni] )
                    {
                        seen[ni] = 1u;
                        pending.emplace( nx, nz );
                    }
                }
            }
            bestSpan = std::max( bestSpan, std::max( maxX - minX + 1, maxZ - minZ + 1 ) );
        }

    CHECK( bestSpan >= 72 );
}

TEST_CASE( default_cave_field_retains_connected_spaghetti_density )
{
    const auto cfg = fieldConfig( "caves", worldgen::FieldDimension::D3,
                                  "caves.lua", 5000u );
    worldgen::LuaFieldEvaluator caves( cfg, 1337u );

    constexpr int N = 64;
    constexpr int CHUNKS = N / static_cast<int>( world::BLOCKS_PER_CHUNK_EDGE );
    static_assert( N % world::BLOCKS_PER_CHUNK_EDGE == 0 );
    const std::size_t volume = static_cast<std::size_t>( N ) * N * N;
    std::vector<std::uint8_t> mask( volume, 0u );
    const auto index = []( int x, int y, int z ) {
        return ( static_cast<std::size_t>( x ) * static_cast<std::size_t>( N ) +
                 static_cast<std::size_t>( y ) ) * static_cast<std::size_t>( N ) +
               static_cast<std::size_t>( z );
    };

    std::size_t carved = 0u;
    for( int cx = 0; cx < CHUNKS; ++cx )
        for( int cy = 0; cy < CHUNKS; ++cy )
            for( int cz = 0; cz < CHUNKS; ++cz )
            {
                const world::ChunkAddress chunk = world::offsetChunk(
                    world::originChunkAddress(), cx - CHUNKS / 2, cy - CHUNKS, cz - CHUNKS / 2 );
                const worldgen::SampledField sampled = caves.sampleChunk( chunk );
                for( std::int64_t lx = 0; lx < world::BLOCKS_PER_CHUNK_EDGE; ++lx )
                    for( std::int64_t ly = 0; ly < world::BLOCKS_PER_CHUNK_EDGE; ++ly )
                        for( std::int64_t lz = 0; lz < world::BLOCKS_PER_CHUNK_EDGE; ++lz )
                            if( sampled.at3D( lx, ly, lz ) < 0.165 )
                            {
                                const int x = cx * static_cast<int>( world::BLOCKS_PER_CHUNK_EDGE ) + static_cast<int>( lx );
                                const int y = cy * static_cast<int>( world::BLOCKS_PER_CHUNK_EDGE ) + static_cast<int>( ly );
                                const int z = cz * static_cast<int>( world::BLOCKS_PER_CHUNK_EDGE ) + static_cast<int>( lz );
                                mask[index( x, y, z )] = 1u;
                                ++carved;
                            }
            }

    CHECK( carved > volume / 100 );
    CHECK( carved < volume / 6 );

    std::vector<std::uint8_t> seen( volume, 0u );
    std::size_t largest = 0u;
    int largestSpanX = 0, largestSpanY = 0, largestSpanZ = 0;
    constexpr std::array<std::array<int, 3>, 6> dirs{{
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    }};

    for( int sx = 0; sx < N; ++sx )
        for( int sy = 0; sy < N; ++sy )
            for( int sz = 0; sz < N; ++sz )
            {
                const std::size_t start = index( sx, sy, sz );
                if( !mask[start] || seen[start] ) continue;

                std::queue<std::array<int, 3>> pending;
                pending.push( { sx, sy, sz } );
                seen[start] = 1u;
                std::size_t count = 0u;
                int minX = sx, maxX = sx, minY = sy, maxY = sy, minZ = sz, maxZ = sz;
                while( !pending.empty() )
                {
                    const auto p = pending.front();
                    pending.pop();
                    ++count;
                    minX = std::min( minX, p[0] ); maxX = std::max( maxX, p[0] );
                    minY = std::min( minY, p[1] ); maxY = std::max( maxY, p[1] );
                    minZ = std::min( minZ, p[2] ); maxZ = std::max( maxZ, p[2] );
                    for( const auto &d : dirs )
                    {
                        const int nx = p[0] + d[0], ny = p[1] + d[1], nz = p[2] + d[2];
                        if( nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N ) continue;
                        const std::size_t ni = index( nx, ny, nz );
                        if( mask[ni] && !seen[ni] )
                        {
                            seen[ni] = 1u;
                            pending.push( { nx, ny, nz } );
                        }
                    }
                }
                if( count > largest )
                {
                    largest = count;
                    largestSpanX = maxX - minX + 1;
                    largestSpanY = maxY - minY + 1;
                    largestSpanZ = maxZ - minZ + 1;
                }
            }

    CHECK( largest > carved / 3 );
    CHECK( largestSpanX >= 32 );
    CHECK( largestSpanY >= 32 );
    CHECK( largestSpanZ >= 32 );
}

TEST_CASE( river_tunnel_route_is_disabled_outside_real_massifs )
{
    auto tunnelCfg = fieldConfig( "river_tunnel_mask", worldgen::FieldDimension::D2,
                                  "river_route.lua", 1000u );
    tunnelCfg.functionName = "tunnel_mask";
    auto mountainCfg = fieldConfig( "high_mountains_mask", worldgen::FieldDimension::D2,
                                    "high_mountains_mask.lua", 1000u );
    worldgen::LuaFieldEvaluator tunnel( tunnelCfg, 1337u );
    worldgen::LuaFieldEvaluator mountain( mountainCfg, 1337u );

    std::size_t lowlandSamples = 0u;
    std::size_t massifSamples = 0u;
    std::size_t activeTunnelSamples = 0u;
    // Macro geology needs a world-scale sample window; a local spawn window
    // is intentionally too small to contain every massif class now.
    for( int x = -65536; x <= 65536; x += 1024 )
        for( int z = -65536; z <= 65536; z += 1024 )
        {
            const world::BlockAddress p = world::fromOriginOffset( x, 0, z );
            const double mountainValue = mountain.sample2D( p );
            const double tunnelValue = tunnel.sample2D( p );
            if( mountainValue < 0.05 )
            {
                ++lowlandSamples;
                CHECK( tunnelValue >= 0.999999 );
            }
            if( mountainValue > 0.55 )
            {
                ++massifSamples;
                if( tunnelValue < 0.999999 ) ++activeTunnelSamples;
            }
        }

    CHECK( lowlandSamples > 100u );
    CHECK( massifSamples > 0u );
    CHECK( activeTunnelSamples > 0u );
}

TEST_CASE( macro_hydrology_keeps_ocean_and_inland_water_in_sane_world_scale_bands )
{
    auto oceanCfg = fieldConfig( "ocean_mask", worldgen::FieldDimension::D2,
                                 "hydrology.lua", 1000u );
    oceanCfg.functionName = "ocean_mask";
    auto lakeCfg = fieldConfig( "lake_mask", worldgen::FieldDimension::D2,
                                "hydrology.lua", 1000u );
    lakeCfg.functionName = "lake_mask";
    auto levelCfg = fieldConfig( "lake_level", worldgen::FieldDimension::D2,
                                 "hydrology.lua", 1000u );
    levelCfg.functionName = "lake_level";
    auto bottomCfg = fieldConfig( "lake_bottom", worldgen::FieldDimension::D2,
                                  "hydrology.lua", 1000u );
    bottomCfg.functionName = "lake_bottom";

    worldgen::LuaFieldEvaluator ocean( oceanCfg, 1337u );
    worldgen::LuaFieldEvaluator lake( lakeCfg, 1337u );
    worldgen::LuaFieldEvaluator level( levelCfg, 1337u );
    worldgen::LuaFieldEvaluator bottom( bottomCfg, 1337u );

    std::size_t samples = 0u, oceanSamples = 0u, lakeSamples = 0u;
    for( int gx = -64; gx < 64; ++gx )
        for( int gz = -64; gz < 64; ++gz )
        {
            const world::BlockAddress p = world::fromOriginOffset(
                static_cast<std::int64_t>( gx ) * 2048, 0,
                static_cast<std::int64_t>( gz ) * 2048 );
            const double oceanValue = ocean.sample2D( p );
            const double lakeValue = lake.sample2D( p );
            if( oceanValue > 0.45 ) ++oceanSamples;
            else if( lakeValue > 0.48 )
            {
                ++lakeSamples;
                CHECK( level.sample2D( p ) - bottom.sample2D( p ) > 3.0 );
            }
            ++samples;
        }

    const double oceanFraction = static_cast<double>( oceanSamples ) / samples;
    const double lakeFraction = static_cast<double>( lakeSamples ) / samples;
    const double dryLandFraction = 1.0 - oceanFraction - lakeFraction;

    // Guard rails rather than an atlas: seeds may vary, but the default seed
    // must be neither a waterworld nor an almost-waterless supercontinent.
    CHECK( oceanFraction > 0.35 );
    CHECK( oceanFraction < 0.75 );
    CHECK( lakeFraction > 0.003 );
    CHECK( lakeFraction < 0.06 );
    CHECK( dryLandFraction > 0.20 );
}

int main() { return test::runAll(); }
