    erosion.type = worldgen::PassType::Surface;
    erosion.stage = "erosion";
    erosion.blockId = "core:air";
    erosion.field = "height";
    erosion.priority = -1000;
    erosion.replaceBlocks = { "core:stone" };
    cfg.passes.push_back( erosion );

    worldgen::PassConfig addon;
    addon.id = "addon_surface";
    addon.type = worldgen::PassType::Surface;
    addon.stage = "addon";
    addon.blockId = "core:grass";
    addon.field = "height";
    addon.priority = -2000;
    addon.replaceBlocks = { "core:air" };
    cfg.passes.push_back( addon );

    worldgen::WorldGen gen( cfg, blocks, table );
    std::vector<std::uint16_t> ids( static_cast<std::size_t>( world::chunkVolume() ) );
    gen.generateChunkIds( world::originChunkAddress(), ids );

    constexpr std::int64_t edge = world::BLOCKS_PER_CHUNK_EDGE;
    const std::size_t surfaceIndex = static_cast<std::size_t>( 10 * edge );
    CHECK_EQ( ids[surfaceIndex], table.indexOf( "core:grass" ) );
}

TEST_CASE(default_sugar_cane_is_decoration_on_final_river_bank_surface)
{
    world::BlockRegistry blocks;
    world::BiomeRegistry biomes;
    world::ResourceRegistry resources;
    world::RegistryLoader::loadFromDirectory( OMNIGRID_DATA_DIR, blocks, biomes, resources );
    world::BlockIdTable table( blocks );
    const auto cfg = worldgen::loadWorldGenConfig(
        std::filesystem::path( OMNIGRID_DATA_DIR ) / "worldgen.json" );

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


TEST_CASE(decoration_anchor_lattice_does_not_repeat_between_sections)
{
    auto blocks = testfixture::blocks();
    world::BlockIdTable table( blocks );

    worldgen::WorldGenConfig cfg;
    cfg.seed = 1337;
    cfg.workerThreads = 1;
    cfg.surfaceField = "height";
    cfg.stages.push_back( worldgen::StageConfig{ "terrain", 0 } );

    worldgen::FieldConfig height;
    height.id = "height";
    height.dimension = worldgen::FieldDimension::D2;
    height.scriptPath = std::filesystem::path( OMNIGRID_TEST_DATA_DIR ) / "constant_zero.lua";
    cfg.fields.push_back( height );

    worldgen::PassConfig stone;
    stone.id = "stone";
    stone.type = worldgen::PassType::FillBelow;
    stone.stage = "terrain";
    stone.blockId = "core:stone";
    stone.field = "height";
    cfg.passes.push_back( stone );

    worldgen::AnchorSetConfig anchors;
    anchors.id = "markers";
    anchors.surfaceField = "height";
    anchors.surfaceMode = worldgen::AnchorSurfaceMode::Field;
    anchors.spacing = 3;
    anchors.chance = 1.0;
    anchors.salt = 0x12345u;
    cfg.anchorSets.push_back( anchors );

    worldgen::DecorationPassConfig marker;
    marker.id = "markers";
    marker.type = worldgen::DecorationType::Scatter;
    marker.anchorSet = "markers";
    marker.blockId = "core:flower";
    marker.replaceBlocks = { "core:air" };
    cfg.decorations.push_back( marker );

    worldgen::WorldGen gen( cfg, blocks, table );
    std::vector<std::uint16_t> a( static_cast<std::size_t>( world::chunkVolume() ) );
    std::vector<std::uint16_t> b( static_cast<std::size_t>( world::chunkVolume() ) );

    world::ChunkAddress sectionOne{};
    sectionOne.group.section.x = 1;
    world::ChunkAddress sectionTwo = sectionOne;
    sectionTwo.group.section.x = 2;
    gen.generateChunkIds( sectionOne, a );
    gen.generateChunkIds( sectionTwo, b );

    CHECK( a != b );
}

int main(){return test::runAll();}
