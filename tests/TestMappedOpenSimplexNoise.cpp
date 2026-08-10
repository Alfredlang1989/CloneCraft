#include "TestHarness.h"
#include "world/coordinates/Coords.h"
#include "world/worldgen/MappedOpenSimplexNoise.h"
#include "world/worldgen/NoiseSeed.h"
#include "world/worldgen/NoiseSource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    constexpr std::uint64_t WORLD_SEED = 0x123456789ABCDEF0ULL;
    constexpr std::uint64_t FIELD_SALT = 0x3344u;

    worldgen::MacroWarpSettings noMacroWarp()
    {
        worldgen::MacroWarpSettings settings;
        settings.enabled = false;
        return settings;
    }
}

TEST_CASE( mapped_opensimplex_matches_v16_noise_near_origin )
{
    const std::uint64_t fieldSeed = worldgen::deriveNoiseSeed( WORLD_SEED, FIELD_SALT );
    worldgen::MappedOpenSimplexNoise mapped( WORLD_SEED, fieldSeed, noMacroWarp() );
    worldgen::NoiseSource direct2( worldgen::deriveNoiseSeed( fieldSeed, 101u ) );
    worldgen::NoiseSource direct3( worldgen::deriveNoiseSeed( fieldSeed, 501u ) );

    double maxError2 = 0.0;
    double maxError3 = 0.0;
    for( std::int64_t x = -640; x <= 640; x += 17 )
        for( std::int64_t z = -640; z <= 640; z += 23 )
        {
            constexpr std::int64_t y = 37;
            const world::BlockAddress sample = world::fromOriginOffset( x, y, z );
            const double expected2 = direct2.noise2( static_cast<double>( x ) * 0.0048,
                                                     static_cast<double>( z ) * 0.0048 );
            mapped.setSample( sample );
            const double actual2 = mapped.noise2( 0.0048, 101u );
            maxError2 = std::max( maxError2, std::abs( expected2 - actual2 ) );

            const double expected3 = direct3.noise3( static_cast<double>( x ) * 0.008,
                                                     static_cast<double>( y ) * 0.006,
                                                     static_cast<double>( z ) * 0.008 );
            mapped.setSample( sample );
            const double actual3 = mapped.noise3( 0.008, 0.006, 0.008, 501u );
            maxError3 = std::max( maxError3, std::abs( expected3 - actual3 ) );
        }

    // The mapper combines the linear transform before evaluation, so tiny
    // roundoff differences are allowed; morphology must remain numerically
    // indistinguishable from the old v16 path.
    CHECK( maxError2 < 1e-9 );
    CHECK( maxError3 < 1e-9 );
}

TEST_CASE( mapped_phase_is_continuous_across_chunkgroup_boundary )
{
    const std::uint64_t fieldSeed = worldgen::deriveNoiseSeed( WORLD_SEED, FIELD_SALT );
    worldgen::MappedOpenSimplexNoise noise( WORLD_SEED, fieldSeed, noMacroWarp() );
    const world::BlockAddress left = world::fromOriginOffset( world::BLOCKS_PER_GROUP_EDGE - 1,
                                                              19, -7 );
    const world::BlockAddress right = world::offsetBlock( left, 1, 0, 0 );

    noise.setSample( left );
    const double a2 = noise.noise2( 0.0048, 101u, 1.0, 0.0 );
    noise.setSample( right );
    const double b2 = noise.noise2( 0.0048, 101u, 0.0, 0.0 );
    CHECK( std::abs( a2 - b2 ) < 1e-9 );

    noise.setSample( left );
    const double a3 = noise.noise3( 0.03, 0.02, 0.03, 504u, 1.0, 0.0, 0.0 );
    noise.setSample( right );
    const double b3 = noise.noise3( 0.03, 0.02, 0.03, 504u, 0.0, 0.0, 0.0 );
    CHECK( std::abs( a3 - b3 ) < 1e-9 );
}

TEST_CASE( huge_sector_keeps_per_block_noise_variation )
{
    const std::uint64_t fieldSeed = worldgen::deriveNoiseSeed( WORLD_SEED, FIELD_SALT );
    worldgen::MappedOpenSimplexNoise noise( WORLD_SEED, fieldSeed );
    world::BlockAddress a{};
    a.chunk.group.sector = { 8000000000000000000LL, 0, -8000000000000000000LL };
    a.chunk.group.region = { world::REGIONS_PER_SECTOR_EDGE - 1, 2, 3 };
    a.chunk.group.group = { world::GROUPS_PER_REGION_EDGE - 1, 5, 7 };
    a.chunk.chunk = { world::CHUNKS_PER_GROUP_EDGE - 1, 4, 9 };
    a.block = { world::BLOCKS_PER_CHUNK_EDGE - 1, 8, 11 };
    const world::BlockAddress b = world::offsetBlock( a, 1, 0, 0 );

    noise.setSample( a );
    const double va = noise.noise2( 0.0048, 101u );
    noise.setSample( b );
    const double vb = noise.noise2( 0.0048, 101u );
    CHECK( std::isfinite( va ) );
    CHECK( std::isfinite( vb ) );
    CHECK( std::abs( va - vb ) > 1e-12 );
}

TEST_CASE( macro_warp_is_continuous_across_macro_cell_boundary )
{
    const std::uint64_t fieldSeed = worldgen::deriveNoiseSeed( WORLD_SEED, FIELD_SALT );
    worldgen::MappedOpenSimplexNoise noise( WORLD_SEED, fieldSeed );

    // Macro cells are 32 ChunkGroups wide by default. With the current
    // 16-groups-per-region radix, region=1/group=15 is group 31. The next
    // block crosses both a ChunkGroup boundary and the macro-cell boundary.
    world::BlockAddress left{};
    left.chunk.group.region.x = 1;
    left.chunk.group.group.x = world::GROUPS_PER_REGION_EDGE - 1;
    left.chunk.chunk.x = world::CHUNKS_PER_GROUP_EDGE - 1;
    left.block.x = world::BLOCKS_PER_CHUNK_EDGE - 1;
    left.block.z = 7;
    const world::BlockAddress right = world::offsetBlock( left, 1, 0, 0 );

    // Evaluate the same OpenSimplex-domain X location once from the left sample
    // via a +1 local offset and once from the canonical right sample. The only
    // difference is the tiny low-pass warp advance by one world block. A hash
    // seam would make this jump orders of magnitude larger.
    noise.setSample( left );
    const double fromLeft2 = noise.noise2( 0.0048, 101u, 1.0, 0.0 );
    noise.setSample( right );
    const double fromRight2 = noise.noise2( 0.0048, 101u );
    CHECK( std::abs( fromLeft2 - fromRight2 ) < 1e-3 );

    noise.setSample( left );
    const double fromLeft3 = noise.noise3( 0.03, 0.02, 0.03, 504u, 1.0, 0.0, 0.0 );
    noise.setSample( right );
    const double fromRight3 = noise.noise3( 0.03, 0.02, 0.03, 504u );
    CHECK( std::abs( fromLeft3 - fromRight3 ) < 1e-3 );
}

TEST_CASE( hierarchy_macro_warp_breaks_distant_same_local_pattern )
{
    const std::uint64_t fieldSeed = worldgen::deriveNoiseSeed( WORLD_SEED, FIELD_SALT );
    worldgen::MappedOpenSimplexNoise noise( WORLD_SEED, fieldSeed );
    world::BlockAddress near{};
    near.chunk.chunk = { 3, 2, 7 };
    near.block = { 11, 5, 13 };
    world::BlockAddress far = near;
    far.chunk.group.sector.x = 3456789012345678901LL;
    far.chunk.group.sector.z = -2345678901234567890LL;

    noise.setSample( near );
    const double a = noise.noise2( 0.00034, 111u );
    noise.setSample( far );
    const double b = noise.noise2( 0.00034, 111u );
    CHECK( std::abs( a - b ) > 1e-9 );
}

TEST_CASE( coordinate_top_edge_never_wraps )
{
    world::BlockAddress a{};
    a.chunk.group.sector.x = std::numeric_limits<std::int64_t>::max();
    a.chunk.group.region.x = world::REGIONS_PER_SECTOR_EDGE - 1;
    a.chunk.group.group.x = world::GROUPS_PER_REGION_EDGE - 1;
    a.chunk.chunk.x = world::CHUNKS_PER_GROUP_EDGE - 1;
    a.block.x = world::BLOCKS_PER_CHUNK_EDGE - 1;
    world::BlockAddress out{};
    CHECK( !world::tryOffsetBlock( a, 1, 0, 0, out ) );
}

int main() { return test::runAll(); }
