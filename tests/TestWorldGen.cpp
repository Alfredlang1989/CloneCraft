#include "TestHarness.h"
#include "TestWorldGenFixture.h"
#include "world/coordinates/Coords.h"
#include "world/worldgen/WorldGen.h"
#include "world/worldgen/WorldGenConfigLoader.h"
#include "world/registry/RegistryLoader.h"
#include <algorithm>
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
    stone.blockId = "core:stone";
    stone.field = "height";
    stone.priority = 0;
    cfg.passes.push_back( stone );

    worldgen::PassConfig carve;
    carve.id = "carve";
    carve.type = worldgen::PassType::SurfaceLayer;
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
    for( int gx = -8; gx <= 8; ++gx )
        for( int gz = -8; gz <= 8; ++gz )
        {
            const world::BlockAddress column = world::fromOriginOffset(
                static_cast<std::int64_t>( gx ) * 512, 0,
                static_cast<std::int64_t>( gz ) * 512 );
            heights.push_back( gen.surfaceHeight( column ) );
        }

    const auto [minIt, maxIt] = std::minmax_element( heights.begin(), heights.end() );
    CHECK( *minIt < 40 );
    CHECK( *maxIt > 120 );
}

TEST_CASE(default_lowland_river_has_sloped_bed_low_banks_and_sealed_substrate)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
    world::RegistryLoader::loadFromDirectory( CLONECRAFT_DATA_DIR, blocks, biomes, resources );
    world::BlockIdTable table( blocks );
    auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( CLONECRAFT_DATA_DIR ) / "worldgen.json" );
    worldgen::WorldGen gen( cfg, blocks, table, biomes );

    const auto blockAtGlobal = [&]( std::int64_t x, std::int64_t y, std::int64_t z ) {
        const world::BlockAddress point = world::fromOriginOffset( x, y, z );
        std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
        gen.generateChunkIds( point.chunk, ids );
        const std::size_t edge = static_cast<std::size_t>( world::BLOCKS_PER_CHUNK_EDGE );
        const std::size_t index =
            ( ( static_cast<std::size_t>( point.block.x ) * edge +
                static_cast<std::size_t>( point.block.y ) ) * edge +
              static_cast<std::size_t>( point.block.z ) );
        return ids[index];
    };
    const auto firstBedY = [&]( std::int64_t x, std::int64_t z ) {
        const std::uint16_t water = table.indexOf( "core:water" );
        std::int64_t y = 0;
        while( y > -32 && blockAtGlobal( x, y, z ) == water ) --y;
        return y;
    };
    const auto isSediment = [&]( std::uint16_t id ) {
        return id == table.indexOf( "core:sand" ) ||
               id == table.indexOf( "core:clay" ) ||
               id == table.indexOf( "core:gravel" ) ||
               id == table.indexOf( "core:dirt" );
    };

    // Stable seed-1337 cross-section. Both samples are water, but the centre-ish
    // sample is markedly deeper than the shore sample: the bed is a U/V profile,
    // not the former flat |_| channel floor.
    CHECK_EQ( blockAtGlobal( -459, 0, 80 ), table.indexOf( "core:water" ) );
    CHECK_EQ( blockAtGlobal( -459, 0, 107 ), table.indexOf( "core:water" ) );
    const std::int64_t deepBed = firstBedY( -459, 80 );
    const std::int64_t shoreBed = firstBedY( -459, 107 );
    CHECK( deepBed <= shoreBed - 3 );
    CHECK( isSediment( blockAtGlobal( -459, deepBed, 80 ) ) );
    CHECK( isSediment( blockAtGlobal( -459, shoreBed, 107 ) ) );

    // Inner bank begins at water level, not on a uniform +2 sand shelf.
    CHECK_EQ( blockAtGlobal( -459, 1, -24 ), table.indexOf( "core:air" ) );
    CHECK_EQ( blockAtGlobal( -459, 0, -24 ), table.indexOf( "core:sand" ) );
    CHECK_EQ( blockAtGlobal( -459, 0, -20 ), table.indexOf( "core:water" ) );
    // A little farther out the bank is allowed to rise by one block.
    CHECK_EQ( blockAtGlobal( -459, 1, -36 ), table.indexOf( "core:sand" ) );

    // This shore position has a real generated cave immediately below the
    // river plug. The first several blocks under the bed must stay sealed;
    // only below that deliberately finite plug may the cave remain air.
    CHECK_EQ( shoreBed, -2 );
    for( std::int64_t y = shoreBed; y >= -9; --y )
        CHECK( blockAtGlobal( -459, y, 107 ) != table.indexOf( "core:air" ) );
    CHECK_EQ( blockAtGlobal( -459, -10, 107 ), table.indexOf( "core:air" ) );
}

TEST_CASE(default_river_uses_continuous_profile_and_sealing_passes)
{
    const auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( CLONECRAFT_DATA_DIR ) / "worldgen.json" );

    const auto findPass = [&]( const std::string &id ) -> const worldgen::PassConfig * {
        const auto it = std::find_if( cfg.passes.begin(), cfg.passes.end(),
            [&]( const worldgen::PassConfig &pass ) { return pass.id == id; } );
        return it == cfg.passes.end() ? nullptr : &*it;
    };
    const auto contains = []( const std::vector<std::string> &values,
                              const std::string &needle ) {
        return std::find( values.begin(), values.end(), needle ) != values.end();
    };

    const worldgen::PassConfig *valley = findPass( "river_valley_open" );
    CHECK( valley != nullptr );
    if( valley != nullptr )
    {
        CHECK( valley->type == worldgen::PassType::SurfaceLayer );
        CHECK( valley->stage == worldgen::PassStage::Addon );
        CHECK_EQ( valley->blockId, std::string( "core:air" ) );
        CHECK_EQ( valley->surfaceField, std::string( "surface_height" ) );
        CHECK_EQ( valley->bottomField, std::string( "river_valley_floor" ) );
        CHECK_EQ( valley->maskField, std::string( "river_valley_mask" ) );
        CHECK( contains( valley->replaceTags, "terrain:carvable" ) );
    }
    for( const char *legacy : { "river_valley_rim", "river_valley_outer",
                                "river_valley_middle", "river_valley_inner",
                                "river_valley_center" } )
        CHECK( findPass( legacy ) == nullptr );

    const worldgen::PassConfig *substrate = findPass( "river_substrate" );
    CHECK( substrate != nullptr );
    if( substrate != nullptr )
    {
        CHECK( substrate->stage == worldgen::PassStage::Addon );
        CHECK_EQ( substrate->blockId, std::string( "core:dirt" ) );
        CHECK_EQ( substrate->surfaceField, std::string( "river_bed_surface" ) );
        CHECK( substrate->thickness >= 6 );
        CHECK( contains( substrate->replaceBlocks, "core:air" ) );
        CHECK( contains( substrate->replaceTags, "terrain:carvable" ) );
    }

    const worldgen::PassConfig *bankFoundation = findPass( "river_bank_foundation" );
    CHECK( bankFoundation != nullptr );
    if( bankFoundation != nullptr )
    {
        CHECK_EQ( bankFoundation->surfaceField, std::string( "river_bank_surface" ) );
        CHECK( contains( bankFoundation->replaceBlocks, "core:air" ) );
    }

    for( const char *id : { "river_bed_sand", "river_bed_clay", "river_bed_gravel" } )
    {
        const worldgen::PassConfig *pass = findPass( id );
        CHECK( pass != nullptr );
        if( pass == nullptr ) continue;
        CHECK_EQ( pass->surfaceField, std::string( "river_bed_surface" ) );
        CHECK_EQ( pass->thickness, 2 );
    }

    const worldgen::PassConfig *water = findPass( "river_water" );
    CHECK( water != nullptr );
    if( water != nullptr )
    {
        CHECK( water->stage == worldgen::PassStage::Addon );
        CHECK_EQ( water->surfaceField, std::string( "river_level" ) );
        CHECK_EQ( water->bottomField, std::string( "river_water_bottom" ) );
        // Air replacement is intentional now: the substrate pass runs first and
        // seals cave openings, then water occupies only the U/V channel above it.
        CHECK( contains( water->replaceBlocks, "core:air" ) );
    }

    const worldgen::PassConfig *caves = findPass( "caves" );
    CHECK( caves != nullptr );
    if( caves != nullptr ) CHECK( caves->stage == worldgen::PassStage::Addon );

    CHECK( findPass( "river_tunnel_floor_space" ) == nullptr );
    for( const char *id : { "river_tunnel_outer", "river_tunnel_middle",
                            "river_tunnel_inner", "river_tunnel_center" } )
    {
        const worldgen::PassConfig *pass = findPass( id );
        CHECK( pass != nullptr );
        if( pass == nullptr ) continue;
        CHECK( pass->type == worldgen::PassType::SurfaceLayer );
        CHECK_EQ( pass->blockId, std::string( "core:air" ) );
        CHECK_EQ( pass->maskField, std::string( "river_tunnel_mask" ) );
        CHECK_EQ( pass->surfaceField, pass->thicknessField );
    }
}

TEST_CASE(pass_stage_barrier_runs_addons_after_terrain_even_with_lower_priority)
{
    auto blocks = testfixture::blocks();
    world::BlockIdTable table( blocks );

    worldgen::WorldGenConfig cfg;
    cfg.seed = 1337;
    cfg.workerThreads = 1;
    cfg.surfaceField = "height";

    worldgen::FieldConfig height;
    height.id = "height";
    height.dimension = worldgen::FieldDimension::D2;
    height.scriptPath = std::filesystem::path( CLONECRAFT_TEST_DATA_DIR ) / "constant_height_10.lua";
    cfg.fields.push_back( height );

    worldgen::PassConfig terrain;
    terrain.id = "terrain";
    terrain.type = worldgen::PassType::FillBelow;
    terrain.stage = worldgen::PassStage::Terrain;
    terrain.blockId = "core:stone";
    terrain.field = "height";
    terrain.priority = 1000;
    cfg.passes.push_back( terrain );

    worldgen::PassConfig addon;
    addon.id = "addon_carve";
    addon.type = worldgen::PassType::Surface;
    addon.stage = worldgen::PassStage::Addon;
    addon.blockId = "core:air";
    addon.field = "height";
    addon.priority = -1000;
    addon.replaceBlocks = { "core:stone" };
    cfg.passes.push_back( addon );

    worldgen::WorldGen gen( cfg, blocks, table );
    std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
    gen.generateChunkIds( world::originChunkAddress(), ids );

    constexpr std::int64_t edge = world::BLOCKS_PER_CHUNK_EDGE;
    const std::size_t surfaceIndex = static_cast<std::size_t>( 10 * edge );
    CHECK_EQ( ids[surfaceIndex], table.indexOf( "core:air" ) );
}

TEST_CASE(default_sugar_cane_is_decoration_on_final_river_bank_surface)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
    world::RegistryLoader::loadFromDirectory( CLONECRAFT_DATA_DIR, blocks, biomes, resources );
    world::BlockIdTable table( blocks );
    const auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( CLONECRAFT_DATA_DIR ) / "worldgen.json" );

    const auto anchorIt = std::find_if( cfg.anchorSets.begin(), cfg.anchorSets.end(),
        []( const worldgen::AnchorSetConfig &anchor ) { return anchor.id == "river_reeds"; } );
    CHECK( anchorIt != cfg.anchorSets.end() );
    if( anchorIt != cfg.anchorSets.end() )
    {
        CHECK( anchorIt->surfaceMode == worldgen::AnchorSurfaceMode::Postprocess );
        CHECK_EQ( anchorIt->surfaceField, std::string( "surface_height" ) );
    }

    const auto caneIt = std::find_if( cfg.decorations.begin(), cfg.decorations.end(),
        []( const worldgen::DecorationPassConfig &decoration ) { return decoration.id == "sugar_cane"; } );
    CHECK( caneIt != cfg.decorations.end() );
    if( caneIt != cfg.decorations.end() )
        CHECK_EQ( caneIt->anchorSet, std::string( "river_reeds" ) );

    worldgen::WorldGen gen( cfg, blocks, table, biomes );
    const std::uint16_t cane = table.indexOf( "core:sugar_cane" );
    std::size_t caneBlocks = 0u;
    for( std::int64_t x = -480; x <= -432; x += world::BLOCKS_PER_CHUNK_EDGE )
        for( std::int64_t z = 32; z <= 128; z += world::BLOCKS_PER_CHUNK_EDGE )
        {
            const world::ChunkAddress chunk = world::fromOriginOffset( x, 0, z ).chunk;
            std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
            gen.generateChunkIds( chunk, ids );
            caneBlocks += static_cast<std::size_t>( std::count( ids.begin(), ids.end(), cane ) );
        }
    CHECK( caneBlocks > 0u );
}

int main(){return test::runAll();}
