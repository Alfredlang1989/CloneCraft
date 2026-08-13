#include "TestHarness.h"
#include "TestWorldGenFixture.h"
#include "world/coordinates/Coords.h"
#include "world/worldgen/WorldGen.h"
#include "world/worldgen/WorldGenConfigLoader.h"
#include "world/worldgen/LuaFieldEvaluator.h"
#include "world/registry/RegistryLoader.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

TEST_CASE(worldgen_generates_from_hierarchical_address)
{
    auto blocks=testfixture::blocks(); world::BlockIdTable table(blocks);
    auto cfg=testfixture::config(); worldgen::WorldGen gen(cfg,blocks,table);
    std::vector<std::uint16_t> ids(static_cast<std::size_t>(world::chunkVolume()));
    const auto count=gen.generateChunkIds(world::originChunkAddress(),ids);
    CHECK(count>0u); CHECK(count<ids.size());
}

TEST_CASE(worldgen_is_deterministic_at_huge_sector)
{
    auto blocks=testfixture::blocks(); world::BlockIdTable table(blocks);
    auto cfg=testfixture::config(); worldgen::WorldGen gen(cfg,blocks,table);
    world::ChunkAddress c{}; c.group.sector={8000000000000000000LL,0,-8000000000000000000LL};
    std::vector<std::uint16_t> a(static_cast<std::size_t>(world::chunkVolume()));
    std::vector<std::uint16_t> b(a.size());
    gen.generateChunkIds(c,a); gen.generateChunkIds(c,b); CHECK(a==b);
}

TEST_CASE(surface_height_uses_mapped_opensimplex_without_global_double)
{
    auto blocks=testfixture::blocks(); world::BlockIdTable table(blocks);
    auto cfg=testfixture::config(); worldgen::WorldGen gen(cfg,blocks,table);
    world::BlockAddress a{}; a.chunk.group.sector.x=7999999999999999999LL;
    a.chunk.group.region.x=15; a.chunk.group.group.x=15; a.chunk.chunk.x=15; a.block.x=15;
    const auto b=world::offsetBlock(a,1,0,0);
    const auto ha=gen.surfaceHeight(a); const auto hb=gen.surfaceHeight(b);
    CHECK(ha>=3 && ha<=7); CHECK(hb>=3 && hb<=7);
}
TEST_CASE(surface_layer_bottom_field_carves_only_down_to_explicit_floor)
{
    auto blocks = testfixture::blocks();
    world::BlockIdTable table( blocks );

    worldgen::WorldGenConfig cfg;
    cfg.seed = 1337;
    cfg.workerThreads = 1;
    cfg.surfaceField = "height";
    cfg.stages.push_back( worldgen::StageConfig{ "terrain", 0 } );
    cfg.stages.push_back( worldgen::StageConfig{ "addon", 1 } );

    worldgen::FieldConfig height;
    height.id = "height";
    height.dimension = worldgen::FieldDimension::D2;
    height.scriptPath = std::filesystem::path( CLONECRAFT_TEST_DATA_DIR ) / "constant_height_10.lua";
    cfg.fields.push_back( height );

    worldgen::FieldConfig floor = height;
    floor.id = "floor";
    floor.scriptPath = std::filesystem::path( CLONECRAFT_TEST_DATA_DIR ) / "constant_zero.lua";
    cfg.fields.push_back( floor );

    worldgen::FieldConfig mask = floor;
    mask.id = "mask";
    cfg.fields.push_back( mask );

    worldgen::PassConfig stone;
    stone.id = "stone";
    stone.type = worldgen::PassType::FillBelow;
    stone.stage = "terrain";
    stone.blockId = "core:stone";
    stone.field = "height";
    stone.priority = 0;
    cfg.passes.push_back( stone );

    worldgen::PassConfig carve;
    carve.id = "carve";
    carve.type = worldgen::PassType::SurfaceLayer;
    carve.stage = "addon";
    carve.blockId = "core:air";
    carve.priority = 10;
    carve.surfaceField = "height";
    carve.bottomField = "floor";
    carve.bottomOffset = 1;
    carve.maskField = "mask";
    carve.maskCondition.op = worldgen::CompareOp::Less;
    carve.maskCondition.value = 0.5;
    carve.replaceTags = { "terrain:carvable" };
    cfg.passes.push_back( carve );

    worldgen::WorldGen gen( cfg, blocks, table );
    std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
    gen.generateChunkIds( world::originChunkAddress(), ids );

    constexpr std::int64_t edge = world::BLOCKS_PER_CHUNK_EDGE;
    const auto at = [&]( std::int64_t y ) {
        const std::size_t index = static_cast<std::size_t>( y * edge );
        return ids[index];
    };

    const std::uint16_t air = table.indexOf( "core:air" );
    const std::uint16_t stoneId = table.indexOf( "core:stone" );
    CHECK_EQ( at( 10 ), air );
    CHECK_EQ( at( 1 ), air );
    CHECK_EQ( at( 0 ), stoneId );
}

TEST_CASE(biome_terrain_profile_is_applied_to_surface_height)
{
    auto blocks = testfixture::blocks();
    world::BlockIdTable table( blocks );
    auto cfg = testfixture::config();

    worldgen::FieldConfig mountainMask = cfg.fields.front();
    mountainMask.id = "mountain_mask";
    mountainMask.salt = 2u;
    cfg.fields.push_back( mountainMask );

    world::BiomeRegistry biomes;
    world::BiomeDef plains;
    plains.id = "core:plains";
    plains.displayName = "Plains";
    plains.surfaceBlock = "core:grass";
    plains.fillerBlock = "core:grass";
    plains.terrain.detailMultiplier = 0.0;
    biomes.insert( plains );

    world::BiomeDef mountains;
    mountains.id = "core:test_mountains";
    mountains.displayName = "Test Mountains";
    mountains.surfaceBlock = "core:stone";
    mountains.fillerBlock = "core:stone";
    mountains.terrainMaskField = "mountain_mask";
    mountains.terrain.heightOffset = 100.0;
    mountains.terrain.heightMultiplier = 2.0;
    mountains.terrain.detailMultiplier = 0.0;
    mountains.terrain.ridgeAmplitude = 0.0;
    biomes.insert( mountains );

    worldgen::WorldGen raw( cfg, blocks, table );
    worldgen::WorldGen profiled( cfg, blocks, table, biomes );
    const world::BlockAddress column = world::fromOriginOffset( 17, 0, -9 );
    const std::int64_t rawHeight = raw.surfaceHeight( column );
    const std::int64_t profiledHeight = profiled.surfaceHeight( column );
    CHECK( profiledHeight >= 100 + rawHeight * 2 - 2 );
    CHECK( profiledHeight <= 100 + rawHeight * 2 + 2 );
    CHECK( profiledHeight > rawHeight + 90 );
}

TEST_CASE(default_biome_terrain_has_lowlands_and_real_mountains)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
    world::RegistryLoader::loadFromDirectory( CLONECRAFT_DATA_DIR, blocks, biomes, resources );
    world::BlockIdTable table( blocks );
    const auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( CLONECRAFT_DATA_DIR ) / "worldgen.json" );
    worldgen::WorldGen gen( cfg, blocks, table, biomes );

    std::vector<std::int64_t> heights;
    for( int gx = -24; gx <= 24; ++gx )
        for( int gz = -24; gz <= 24; ++gz )
        {
            const world::BlockAddress column = world::fromOriginOffset(
                static_cast<std::int64_t>( gx ) * 4096, 0,
                static_cast<std::int64_t>( gz ) * 4096 );
            heights.push_back( gen.surfaceHeight( column ) );
        }

    const auto [minIt, maxIt] = std::minmax_element( heights.begin(), heights.end() );
    CHECK( *minIt < 40 );
    CHECK( *maxIt > 120 );
}

TEST_CASE(default_ocean_and_lake_fields_materialize_as_real_water)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
    world::RegistryLoader::loadFromDirectory( CLONECRAFT_DATA_DIR, blocks, biomes, resources );
    world::BlockIdTable table( blocks );
    const auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( CLONECRAFT_DATA_DIR ) / "worldgen.json" );
    worldgen::WorldGen gen( cfg, blocks, table, biomes );

    const auto findField = [&]( const std::string &id ) -> const worldgen::FieldConfig * {
        const auto it = std::find_if( cfg.fields.begin(), cfg.fields.end(),
            [&]( const worldgen::FieldConfig &field ) { return field.id == id; } );
        return it == cfg.fields.end() ? nullptr : &*it;
    };
    const auto *oceanCfg = findField( "ocean_mask" );
    const auto *lakeCfg = findField( "lake_mask" );
    const auto *lakeLevelCfg = findField( "lake_level" );
    CHECK( oceanCfg != nullptr );
    CHECK( lakeCfg != nullptr );
    CHECK( lakeLevelCfg != nullptr );
    if( !oceanCfg || !lakeCfg || !lakeLevelCfg ) return;

    worldgen::LuaFieldEvaluator ocean( *oceanCfg, cfg.seed );
    worldgen::LuaFieldEvaluator lake( *lakeCfg, cfg.seed );
    worldgen::LuaFieldEvaluator lakeLevel( *lakeLevelCfg, cfg.seed );

    const auto blockAt = [&]( const world::BlockAddress &point ) {
        std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
        gen.generateChunkIds( point.chunk, ids );
        const std::size_t edge = static_cast<std::size_t>( world::BLOCKS_PER_CHUNK_EDGE );
        const std::size_t index =
            ( ( static_cast<std::size_t>( point.block.x ) * edge +
                static_cast<std::size_t>( point.block.y ) ) * edge +
              static_cast<std::size_t>( point.block.z ) );
        return ids[index];
    };

    std::optional<world::BlockAddress> oceanPoint;
    for( std::int64_t x = -131072; x <= 131072 && !oceanPoint; x += 4096 )
        for( std::int64_t z = -131072; z <= 131072; z += 4096 )
        {
            const world::BlockAddress p = world::fromOriginOffset( x, 0, z );
            if( ocean.sample2D( p ) > 0.88 ) { oceanPoint = p; break; }
        }
    CHECK( oceanPoint.has_value() );
    if( oceanPoint )
    {
        CHECK( gen.surfaceHeight( *oceanPoint ) < 0 );
        const world::BlockAddress waterPoint = world::withOriginRelativeY( *oceanPoint, 0 );
        CHECK_EQ( blockAt( waterPoint ), table.indexOf( "core:water" ) );
    }

    std::optional<world::BlockAddress> lakePoint;
    for( std::int64_t x = -131072; x <= 131072 && !lakePoint; x += 1024 )
        for( std::int64_t z = -131072; z <= 131072; z += 1024 )
        {
            const world::BlockAddress p = world::fromOriginOffset( x, 0, z );
            if( ocean.sample2D( p ) < 0.20 && lake.sample2D( p ) > 0.72 )
            { lakePoint = p; break; }
        }
    CHECK( lakePoint.has_value() );
    if( lakePoint )
    {
        const std::int64_t y = static_cast<std::int64_t>( std::floor( lakeLevel.sample2D( *lakePoint ) ) );
        const world::BlockAddress waterPoint = world::withOriginRelativeY( *lakePoint, y );
        CHECK_EQ( blockAt( waterPoint ), table.indexOf( "core:water" ) );
    }
}

TEST_CASE(default_lowland_river_has_sloped_bed_low_banks_and_sealed_substrate)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
