#include "TestHarness.h"

#include "world/coordinates/Coords.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/worldgen/WorldGen.h"
#include "world/worldgen/WorldGenConfig.h"
#include "world/worldgen/WorldGenConfigLoader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
    struct RealWorldGen
    {
        world::BlockRegistry blocks;
        world::BiomeRegistry biomes;
        world::ResourceRegistry resources;
        world::BlockIdTable table;
        worldgen::WorldGenConfig cfg;
        std::unique_ptr<worldgen::WorldGen> gen;

        explicit RealWorldGen( std::uint64_t seed )
        {
            world::RegistryLoader::loadFromDirectory( OMNIGRID_DATA_DIR, blocks, biomes, resources );
            table = world::BlockIdTable( blocks );
            cfg = worldgen::loadWorldGenConfig(
                std::filesystem::path( OMNIGRID_DATA_DIR ) / "worldgen.json" );
            cfg.seed = seed;
            cfg.workerThreads = 2u;
            gen = std::make_unique<worldgen::WorldGen>( cfg, blocks, table, biomes );
        }
    };

    double weightOf( const std::vector<worldgen::BiomeWeightSample> &samples,
                     const char *id )
    {
        for( const worldgen::BiomeWeightSample &sample : samples )
            if( sample.id == id ) return sample.weight;
        return 0.0;
    }

    std::string dominantOf( const std::vector<worldgen::BiomeWeightSample> &samples )
    {
        std::string bestId;
        double bestWeight = -1.0;
        for( const worldgen::BiomeWeightSample &sample : samples )
            if( sample.weight > bestWeight )
            {
                bestWeight = sample.weight;
                bestId = sample.id;
            }
        return bestId;
    }

    struct DistributionStats
    {
        std::size_t samples = 0;
        std::size_t desertDominant = 0;
        std::size_t desertPresent = 0;
        std::size_t badlandsDominant = 0;
        std::size_t desertMountainsDominant = 0;
    };

    DistributionStats sampleDistribution( const worldgen::WorldGen &gen, std::int64_t chunkExtent )
    {
        DistributionStats stats;
        const std::int64_t half = chunkExtent / 2;
        constexpr std::int64_t edge = world::BLOCKS_PER_CHUNK_EDGE;
        for( std::int64_t cx = -half; cx < chunkExtent - half; ++cx )
            for( std::int64_t cz = -half; cz < chunkExtent - half; ++cz )
            {
                const world::ChunkAddress chunk =
                    world::offsetChunk( world::originChunkAddress(), cx, 0, cz );
                const world::BlockAddress column =
                    world::blockAt( chunk, { edge / 2, 0, edge / 2 } );
                const std::vector<worldgen::BiomeWeightSample> samples = gen.biomeWeights( column );

                ++stats.samples;
                if( dominantOf( samples ) == "core:desert" ) ++stats.desertDominant;
                if( dominantOf( samples ) == "core:badlands" ) ++stats.badlandsDominant;
                if( dominantOf( samples ) == "core:desert_high_mountains" )
                    ++stats.desertMountainsDominant;
                if( weightOf( samples, "core:desert" ) > 0.1 ) ++stats.desertPresent;
            }
        return stats;
    }

    double fraction( std::size_t count, std::size_t total )
    {
        return total == 0 ? 0.0 : static_cast<double>( count ) / static_cast<double>( total );
    }

    void printStats( const char *label, const DistributionStats &stats )
    {
        std::cerr << label
                  << " desert=" << fraction( stats.desertDominant, stats.samples )
                  << " present=" << fraction( stats.desertPresent, stats.samples )
                  << " badlands=" << fraction( stats.badlandsDominant, stats.samples )
                  << " desertMountains=" << fraction( stats.desertMountainsDominant, stats.samples )
                  << "\n";
    }
}

TEST_CASE( desert_climate_response_has_core_and_transition_coverage_seed_1337 )
{
    const RealWorldGen real( 1337u );
    const DistributionStats stats = sampleDistribution( *real.gen, 128 );
    printStats( "seed 1337", stats );
    const double desert = fraction( stats.desertDominant, stats.samples );
    const double badlands = fraction( stats.badlandsDominant, stats.samples );
    const double desertPresent = fraction( stats.desertPresent, stats.samples );

    // Broad hot*dry response: desert owns a clear share of the map ...
    CHECK( stats.samples >= 4000u );
    CHECK( desert > 0.05 );
    CHECK( desert < 0.30 );
    // ... with a wider transition band (desert present but not dominant).
    CHECK( desertPresent > desert * 2.0 );
    CHECK( desertPresent < 0.80 );
    // Badlands is the rarer, wetter-hot exotic variant of the desert climate.
    CHECK( badlands > 0.002 );
    CHECK( badlands < 0.10 );
    CHECK( badlands < desert );
}

TEST_CASE( desert_climate_response_holds_for_a_second_seed )
{
    const RealWorldGen real( 4242u );
    const DistributionStats stats = sampleDistribution( *real.gen, 128 );
    printStats( "seed 4242", stats );
    const double desert = fraction( stats.desertDominant, stats.samples );
    const double badlands = fraction( stats.badlandsDominant, stats.samples );
    const double desertPresent = fraction( stats.desertPresent, stats.samples );

    CHECK( desert > 0.03 );
    CHECK( desert < 0.30 );
    CHECK( desertPresent > desert );
    CHECK( badlands > 0.002 );
    CHECK( badlands < 0.10 );
    CHECK( badlands < desert );
    CHECK( fraction( stats.desertMountainsDominant, stats.samples ) < desert );
}

TEST_CASE( biome_competition_is_deterministic_per_seed )
{
    const RealWorldGen first( 1337u );
    const RealWorldGen second( 1337u );
    const RealWorldGen otherSeed( 4242u );

    std::size_t differingColumns = 0u;
    for( std::int64_t cx = -8; cx < 8; ++cx )
        for( std::int64_t cz = -8; cz < 8; ++cz )
        {
            const world::ChunkAddress chunk =
                world::offsetChunk( world::originChunkAddress(), cx, 0, cz );
            const world::BlockAddress column =
                world::blockAt( chunk, { world::BLOCKS_PER_CHUNK_EDGE / 2, 0,
                                         world::BLOCKS_PER_CHUNK_EDGE / 2 } );
            const auto a = first.gen->biomeWeights( column );
            const auto b = second.gen->biomeWeights( column );
            CHECK_EQ( a.size(), b.size() );
            for( std::size_t i = 0; i < a.size() && i < b.size(); ++i )
            {
                CHECK_EQ( a[i].id, b[i].id );
                CHECK( std::abs( a[i].weight - b[i].weight ) < 1.0e-9 );
            }

            const auto c = otherSeed.gen->biomeWeights( column );
            bool differs = c.size() != a.size();
            for( std::size_t i = 0; i < c.size() && i < a.size() && !differs; ++i )
                differs = std::abs( c[i].weight - a[i].weight ) > 1.0e-9;
            if( differs ) ++differingColumns;
        }

    CHECK( differingColumns > 0u );
}

TEST_CASE( biome_weights_are_invariant_across_hierarchy_boundaries )
{
    const RealWorldGen real( 1337u );
    constexpr std::int64_t groupEdgeBlocks =
        world::BLOCKS_PER_CHUNK_EDGE * world::CHUNKS_PER_GROUP_EDGE;
    // x = groupEdgeBlocks is the boundary between group 0 and group 1.
    const std::int64_t boundary = groupEdgeBlocks;

    // The same absolute column resolved through different chunk decompositions
    // (straddling chunk, group and section digits) must yield identical weights.
    for( std::int64_t dz = -4; dz <= 4; ++dz )
        for( std::int64_t x : { boundary - 1, boundary, boundary + 1, boundary + 2 } )
        {
            const std::int64_t z = dz * 37;
            const auto viaOffset = real.gen->biomeWeights( world::fromOriginOffset( x, 0, z ) );
            const world::ChunkAddress chunk = world::offsetChunk( world::originChunkAddress(),
                world::floorDiv( x, world::BLOCKS_PER_CHUNK_EDGE ), 0,
                world::floorDiv( z, world::BLOCKS_PER_CHUNK_EDGE ) );
            const auto viaChunk = real.gen->biomeWeights( world::blockAt( chunk,
                { world::floorMod( x, world::BLOCKS_PER_CHUNK_EDGE ), 0,
                  world::floorMod( z, world::BLOCKS_PER_CHUNK_EDGE ) } ) );
            CHECK_EQ( viaChunk.size(), viaOffset.size() );
            for( std::size_t i = 0; i < viaChunk.size() && i < viaOffset.size(); ++i )
            {
                CHECK_EQ( viaChunk[i].id, viaOffset[i].id );
                CHECK( std::abs( viaChunk[i].weight - viaOffset[i].weight ) < 1.0e-12 );
            }
        }

    // Weight change across the group boundary must not stand out against
    // interior weight change: no seam where one hierarchy cell ends.
    auto boundaryDelta = [&]( std::int64_t x ) {
        double sum = 0.0;
        for( std::int64_t z = -512; z < 512; z += 8 )
        {
            const auto left = real.gen->biomeWeights( world::fromOriginOffset( x, 0, z ) );
            const auto right = real.gen->biomeWeights( world::fromOriginOffset( x + 1, 0, z ) );
            for( std::size_t i = 0; i < left.size() && i < right.size(); ++i )
                sum += std::abs( left[i].weight - right[i].weight );
        }
        return sum;
    };

    const double crossing = boundaryDelta( boundary - 1 ) + boundaryDelta( boundary );
    const double interior = boundaryDelta( boundary - 3 ) + boundaryDelta( boundary + 2 );
    CHECK( crossing < interior * 3.0 );
}

TEST_CASE( biome_weight_acts_as_relative_competition_multiplier )
{
    const RealWorldGen real( 1337u );

    // Synthetic config with exactly one 2D field: the same field pipeline the
    // real data uses, but a controlled pass set and a controlled biome pool.
    worldgen::WorldGenConfig cfg;
    cfg.seed = 7u;
    cfg.workerThreads = 1u;
    cfg.surfaceField = "temperature";
    worldgen::StageConfig stage;
    stage.id = "terrain";
    stage.order = 0;
    cfg.stages.push_back( stage );

    const auto field = []( const char *id, const char *script ) {
        worldgen::FieldConfig f;
        f.id = id;
        f.dimension = worldgen::FieldDimension::D2;
        f.scriptPath = std::filesystem::path( OMNIGRID_DATA_DIR ) / "worldgen" / script;
        f.functionName = "sample";
        f.salt = 1000u;
        return f;
    };
    cfg.fields.push_back( field( "temperature", "temperature.lua" ) );
    cfg.fields.push_back( field( "desert_mask", "desert_mask.lua" ) );

    worldgen::PassConfig pass;
    pass.id = "base";
    pass.type = worldgen::PassType::FillBelow;
    pass.stage = "terrain";
    pass.blockId = "core:sand";
    pass.field = "temperature";
    pass.priority = 0;
    cfg.passes.push_back( pass );

    // Two competing biomes with identical selection responses and one shared
    // terrain mask: the resolved weights must be proportional to BiomeDef::weight.
    // A fallback biome with an empty selection band never wins any column.
    auto addBiome = [&]( world::BiomeRegistry &target, const char *id, double weight ) {
        world::BiomeDef biome;
        biome.id = id;
        biome.displayName = id;
        biome.surfaceBlock = "core:sand";
        biome.fillerBlock = "core:sandstone";
        biome.weight = weight;
        biome.selectionFields.push_back( { "temperature", 0.0, 1.0, 0.0 } );
        biome.terrainMaskField = "desert_mask";
        target.insert( biome );
    };

    const auto weightsFor = [&]( double desertWeight, double badlandsWeight ) {
        world::BiomeRegistry synthetic;
        addBiome( synthetic, "core:desert", desertWeight );
        addBiome( synthetic, "core:badlands", badlandsWeight );
        world::BiomeDef fallback;
        fallback.id = "core:plains";
        fallback.displayName = "Plains";
        fallback.surfaceBlock = "core:grass";
        fallback.fillerBlock = "core:grass";
        fallback.selectionFields.push_back( { "temperature", 0.0, 0.0, 0.0 } );
        synthetic.insert( fallback );
        const world::BlockAddress column = world::fromOriginOffset( 0, 0, 0 );
        worldgen::WorldGen gen( cfg, real.blocks, real.table, synthetic );
        return gen.biomeWeights( column );
    };

    const auto equal = weightsFor( 1.0, 1.0 );
    CHECK( std::abs( weightOf( equal, "core:desert" ) - 0.5 ) < 1.0e-9 );
    CHECK( std::abs( weightOf( equal, "core:badlands" ) - 0.5 ) < 1.0e-9 );

    const auto multiplied = weightsFor( 3.0, 1.0 );
    const double ratio = weightOf( multiplied, "core:desert" ) /
                         std::max( weightOf( multiplied, "core:badlands" ), 1.0e-300 );
    CHECK( std::abs( ratio - 3.0 ) < 1.0e-9 );

    const auto inverted = weightsFor( 1.0, 3.0 );
    const double invertedRatio = weightOf( inverted, "core:badlands" ) /
                                 std::max( weightOf( inverted, "core:desert" ), 1.0e-300 );
    CHECK( std::abs( invertedRatio - 3.0 ) < 1.0e-9 );
}

TEST_CASE( biome_weights_are_normalized_per_column )
{
    const RealWorldGen real( 1337u );
    for( std::int64_t cx = -4; cx < 4; ++cx )
        for( std::int64_t cz = -4; cz < 4; ++cz )
        {
            const world::ChunkAddress chunk =
                world::offsetChunk( world::originChunkAddress(), cx, 0, cz );
            const world::BlockAddress column =
                world::blockAt( chunk, { world::BLOCKS_PER_CHUNK_EDGE / 2, 0,
                                         world::BLOCKS_PER_CHUNK_EDGE / 2 } );
            double total = 0.0;
            for( const worldgen::BiomeWeightSample &sample : real.gen->biomeWeights( column ) )
                total += sample.weight;
            CHECK( std::abs( total - 1.0 ) < 1.0e-9 );
        }
}

int main() { return test::runAll(); }
