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
    const std::uint16_t innerBankAbove = blockAtGlobal( -459, 1, -24 );
    CHECK( !blocks.get( table.idOf( innerBankAbove ) ).solid );
    CHECK_EQ( blockAtGlobal( -459, 0, -24 ), table.indexOf( "core:sand" ) );
    CHECK_EQ( blockAtGlobal( -459, 0, -20 ), table.indexOf( "core:water" ) );
    // A little farther out the bank is allowed to rise by one block.
    CHECK_EQ( blockAtGlobal( -459, 1, -36 ), table.indexOf( "core:sand" ) );

    // The first several blocks under the bed must stay sealed. Basin hydrology
    // may legitimately put a lake over this old deterministic cross-section,
    // so a deeper cave cell can now contain water instead of Air.
    CHECK_EQ( shoreBed, -2 );
    for( std::int64_t y = shoreBed; y >= -9; --y )
        CHECK( blockAtGlobal( -459, y, 107 ) != table.indexOf( "core:air" ) );
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
        CHECK( valley->stage == std::string( "addon" ) );
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
        CHECK( substrate->stage == std::string( "addon" ) );
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
        CHECK( water->stage == std::string( "addon" ) );
        CHECK_EQ( water->surfaceField, std::string( "river_level" ) );
        CHECK_EQ( water->bottomField, std::string( "river_water_bottom" ) );
        // Air replacement is intentional now: the substrate pass runs first and
        // seals cave openings, then water occupies only the U/V channel above it.
        CHECK( contains( water->replaceBlocks, "core:air" ) );
    }

    const worldgen::PassConfig *caves = findPass( "caves" );
    CHECK( caves != nullptr );
    if( caves != nullptr ) CHECK( caves->stage == std::string( "addon" ) );

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

TEST_CASE(data_defined_stage_barrier_orders_custom_stages_before_addon)
{
    auto blocks = testfixture::blocks();
    world::BlockIdTable table( blocks );

    worldgen::WorldGenConfig cfg;
    cfg.seed = 1337;
    cfg.workerThreads = 1;
    cfg.surfaceField = "height";
    // Deliberately declare stages out of execution order. The numeric order in
    // stage.json/config is authoritative, not array position or pass priority.
    cfg.stages.push_back( worldgen::StageConfig{ "terrain", 0 } );
    cfg.stages.push_back( worldgen::StageConfig{ "addon", 2 } );
    cfg.stages.push_back( worldgen::StageConfig{ "erosion", 1 } );

    worldgen::FieldConfig height;
    height.id = "height";
    height.dimension = worldgen::FieldDimension::D2;
    height.scriptPath = std::filesystem::path( CLONECRAFT_TEST_DATA_DIR ) / "constant_height_10.lua";
    cfg.fields.push_back( height );

    worldgen::PassConfig terrain;
    terrain.id = "terrain";
    terrain.type = worldgen::PassType::FillBelow;
    terrain.stage = "terrain";
    terrain.blockId = "core:stone";
    terrain.field = "height";
    terrain.priority = 1000;
    cfg.passes.push_back( terrain );

    worldgen::PassConfig erosion;
    erosion.id = "erosion_carve";
