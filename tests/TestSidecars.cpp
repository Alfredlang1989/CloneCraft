#include "TestHarness.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/OrientationSidecar.h"
#include "world/chunk/Sidecar.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <variant>

#ifndef OMNIGRID_DATA_DIR
#define OMNIGRID_DATA_DIR "MODS/Default"
#endif

namespace
{
    using json = nlohmann::json;

    bool rejected( const std::function<void()> &fn )
    {
        try
        {
            fn();
        }
        catch( const world::RegistryError & )
        {
            return true;
        }
        return false;
    }

    world::SidecarRegistry parseSidecars( const std::string &text )
    {
        world::SidecarRegistry out;
        world::RegistryLoader::parseSidecars( json::parse( text ), "test-sidecars.json", out );
        return out;
    }
} // namespace


TEST_CASE( sidecar_is_empty_until_first_set )
{
    world::OrientationSidecar sidecar;
    CHECK( sidecar.empty() );
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 0 } );
    CHECK( !sidecar.get( 0u ).has_value() );
    CHECK( sidecar.defaultValue() == world::BlockOrientation::Up );
}

TEST_CASE( sidecar_lazy_allocation_and_occupation )
{
    world::OrientationSidecar sidecar( world::Chunk::VOLUME );
    CHECK( sidecar.set( 5u, world::BlockOrientation::East ) );
    CHECK( !sidecar.empty() );
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 1 } );
    CHECK( sidecar.get( 5u ) == world::BlockOrientation::East );

    CHECK( !sidecar.set( 4096u, world::BlockOrientation::South ) ); // out of range
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 1 } );
    CHECK( sidecar.get( 5u ) == world::BlockOrientation::East );
    CHECK( !sidecar.get( 4096u ).has_value() );
    CHECK( !sidecar.get( 6u ).has_value() );
}

TEST_CASE( sidecar_rejects_out_of_range_local_index )
{
    world::OrientationSidecar sidecar( world::Chunk::VOLUME );
    CHECK_EQ( sidecar.capacity(), world::Chunk::VOLUME );
    CHECK( sidecar.set( world::Chunk::VOLUME - 1u, world::BlockOrientation::Down ) );
    CHECK( !sidecar.set( world::Chunk::VOLUME, world::BlockOrientation::Down ) );
    CHECK( !sidecar.set( 1000000u, world::BlockOrientation::Down ) );
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 1 } );
    CHECK( !sidecar.get( world::Chunk::VOLUME ).has_value() );
}

TEST_CASE( sidecar_set_reports_whether_state_changed )
{
    world::OrientationSidecar sidecar;
    CHECK( sidecar.empty() );

    CHECK( sidecar.set( 3u, world::BlockOrientation::East ) );   // absent -> entry
    CHECK( !sidecar.set( 3u, world::BlockOrientation::East ) );  // same value: no-op
    CHECK( sidecar.set( 3u, world::BlockOrientation::West ) );   // different value
    CHECK( sidecar.set( 3u, world::BlockOrientation::Up ) );     // back to default
    CHECK( !sidecar.set( 3u, world::BlockOrientation::Up ) );    // default on absent: no-op
    CHECK( sidecar.empty() );
}

TEST_CASE( sidecar_lazy_destruction_when_last_entry_returns_to_default )
{
    world::OrientationSidecar sidecar;
    CHECK( sidecar.set( 5u, world::BlockOrientation::North ) );
    CHECK( !sidecar.empty() );

    CHECK( sidecar.set( 5u, world::BlockOrientation::Up ) );
    CHECK( sidecar.empty() );
    CHECK( !sidecar.get( 5u ).has_value() );
}

TEST_CASE( sidecar_entries_iterate_in_ascending_local_index_order )
{
    world::OrientationSidecar sidecar;
    sidecar.set( 900u, world::BlockOrientation::West );
    sidecar.set( 2u, world::BlockOrientation::Down );
    sidecar.set( 42u, world::BlockOrientation::North );

    std::uint32_t previous = 0u;
    std::size_t visited = 0u;
    for( const auto &[localIndex, orientation] : sidecar.entries() )
    {
        CHECK( localIndex >= previous );
        previous = localIndex;
        ++visited;
        (void)orientation;
    }
    CHECK_EQ( visited, std::size_t{ 3 } );
    CHECK_EQ( sidecar.entries().begin()->first, 2u );
}

TEST_CASE( chunk_has_no_orientation_sidecar_until_first_set )
{
    world::Chunk chunk( world::ChunkAddress{} );
    const std::uint32_t localIndex = world::blockIndex( { 1, 2, 3 } );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
    CHECK( !chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ).has_value() );

    chunk.setBlock( 1, 2, 3, 7u );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue east{ world::blockOrientationValue( world::BlockOrientation::East ) };
    CHECK( chunk.setProperty( localIndex, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) != nullptr );
    CHECK( chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ) == east );
    CHECK( !chunk.getProperty( world::blockIndex( { 1, 2, 4 } ),
                               world::CORE_ORIENTATION_SIDECAR ).has_value() );
}

TEST_CASE( chunk_sidecar_disappears_again_after_last_default_write )
{
    world::Chunk chunk( world::ChunkAddress{} );
    chunk.setBlock( 1, 2, 3, 7u );
    chunk.setBlock( 4, 5, 6, 8u );
    const std::uint32_t first = world::blockIndex( { 1, 2, 3 } );
    const std::uint32_t second = world::blockIndex( { 4, 5, 6 } );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue east{ world::blockOrientationValue( world::BlockOrientation::East ) };
    const world::PropertyValue south{ world::blockOrientationValue( world::BlockOrientation::South ) };
    CHECK( chunk.setProperty( first, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( chunk.setProperty( second, world::CORE_ORIENTATION_SIDECAR, south, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) != nullptr );

    CHECK( chunk.setProperty( first, world::CORE_ORIENTATION_SIDECAR, up, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) != nullptr ); // one entry left

    CHECK( chunk.setProperty( second, world::CORE_ORIENTATION_SIDECAR, up, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr ); // dropped again

    chunk.setProperty( second, world::CORE_ORIENTATION_SIDECAR, south, up );
    CHECK( chunk.clearProperty( world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
}

TEST_CASE( chunk_manager_orientation_roundtrip )
{
    world::ChunkManager manager;
    const world::ChunkAddress chunkAddr{};
    const world::BlockAddress block = world::fromOriginOffset( 3, 7, 9 );

    CHECK( !manager.blockOrientation( block ).has_value() );
    CHECK( manager.chunkOrientationSidecar( chunkAddr ) == nullptr );

    manager.setBlock( block, 1u ); // chunk must exist to hold a sidecar
    manager.setBlockOrientation( block, world::BlockOrientation::West );
    CHECK( manager.blockOrientation( block ) == world::BlockOrientation::West );
    CHECK( manager.chunkOrientationSidecar( chunkAddr ) != nullptr );

    manager.setBlockOrientation( block, world::BlockOrientation::Up );
    CHECK( !manager.blockOrientation( block ).has_value() );
    CHECK( manager.chunkOrientationSidecar( chunkAddr ) == nullptr );

    manager.setBlockOrientation( block, world::BlockOrientation::North );
    manager.clearChunkOrientations( chunkAddr );
    CHECK( manager.chunkOrientationSidecar( chunkAddr ) == nullptr );
}

TEST_CASE( chunk_manager_orientation_never_creates_chunks )
{
    world::ChunkManager manager;
    const world::BlockAddress block = world::fromOriginOffset( 1, 1, 1 );

    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !manager.setBlockOrientation( block, world::BlockOrientation::East ) );
    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !manager.blockOrientation( block ).has_value() );
}

TEST_CASE( chunk_rejects_orientation_on_air_block )
{
    world::Chunk chunk( world::ChunkAddress{} );
    const std::uint32_t localIndex = world::blockIndex( { 1, 2, 3 } );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue east{ world::blockOrientationValue( world::BlockOrientation::East ) };
    CHECK( !chunk.setProperty( localIndex, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
    CHECK( !chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ).has_value() );

    chunk.setBlock( 1, 2, 3, 7u );
    CHECK( chunk.setProperty( localIndex, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) != nullptr );

    chunk.setBlock( 1, 2, 3, 0u ); // back to AIR
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
    CHECK( !chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ).has_value() );
}

TEST_CASE( chunk_set_block_clears_stale_orientation )
{
    world::Chunk chunk( world::ChunkAddress{} );
    chunk.setBlock( 2, 2, 2, 7u );
    const std::uint32_t localIndex = world::blockIndex( { 2, 2, 2 } );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue south{ world::blockOrientationValue( world::BlockOrientation::South ) };
    CHECK( chunk.setProperty( localIndex, world::CORE_ORIENTATION_SIDECAR, south, up ) );
    CHECK( chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ) == south );

    chunk.setBlock( 2, 2, 2, 8u ); // replacement must invalidate orientation
    CHECK( !chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ).has_value() );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );

    chunk.setBlock( 2, 2, 2, 8u ); // same id again: no change, still no sidecar
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
}

TEST_CASE( chunk_assign_blocks_invalidates_orientations )
{
    world::Chunk chunk( world::ChunkAddress{} );
    chunk.setBlock( 1, 1, 1, 7u );
    const std::uint32_t localIndex = world::blockIndex( { 1, 1, 1 } );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue west{ world::blockOrientationValue( world::BlockOrientation::West ) };
    CHECK( chunk.setProperty( localIndex, world::CORE_ORIENTATION_SIDECAR, west, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) != nullptr );

    std::array<std::uint16_t, world::Chunk::VOLUME> blocks{};
    blocks[0] = 3u;
    chunk.assignBlocks( blocks );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
    CHECK( !chunk.getProperty( localIndex, world::CORE_ORIENTATION_SIDECAR ).has_value() );
}

TEST_CASE( chunk_set_property_rejects_out_of_range_local_index )
{
    world::Chunk chunk( world::ChunkAddress{} );
    chunk.setBlock( 1, 2, 3, 7u );
    const world::PropertyValue up{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue east{ world::blockOrientationValue( world::BlockOrientation::East ) };

    // M05 review: the AIR check must not read mBlocks[localIndex] before the
    // index is validated against VOLUME (a future deserialization trap).
    CHECK( !chunk.setProperty( world::Chunk::VOLUME, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( !chunk.setProperty( 1000000u, world::CORE_ORIENTATION_SIDECAR, east, up ) );
    CHECK( chunk.propertySidecar( world::CORE_ORIENTATION_SIDECAR ) == nullptr );
}

TEST_CASE( chunk_manager_set_block_air_clears_orientation )
{
    world::ChunkManager manager;
    const world::BlockAddress block = world::fromOriginOffset( 3, 7, 9 );
    const world::ChunkAddress chunkAddr{};

    manager.setBlock( block, 1u );
    CHECK( manager.setBlockOrientation( block, world::BlockOrientation::East ) );
    CHECK( manager.blockOrientation( block ) == world::BlockOrientation::East );

    manager.setBlock( block, 0u ); // remove the block
    CHECK( !manager.blockOrientation( block ).has_value() );
    CHECK( manager.chunkOrientationSidecar( chunkAddr ) == nullptr );
    CHECK( !manager.setBlockOrientation( block, world::BlockOrientation::East ) ); // AIR rejects
    CHECK( !manager.blockOrientation( block ).has_value() );
}

TEST_CASE( chunk_manager_no_op_orientation_is_not_dirty )
{
    world::ChunkManager manager;
    std::size_t notifications = 0u;
    manager.setOnChunkChange(
        [&]( const world::ChunkAddress & ) { ++notifications; } );

    const world::BlockAddress block = world::fromOriginOffset( 4, 4, 4 );
    manager.setBlock( block, 1u );
    const std::size_t afterSetBlock = notifications;
    CHECK( afterSetBlock > 0u );

    CHECK( !manager.setBlockOrientation( block, world::BlockOrientation::Up ) );
    CHECK_EQ( notifications, afterSetBlock ); // default on default: no change

    CHECK( manager.setBlockOrientation( block, world::BlockOrientation::East ) );
    CHECK_EQ( notifications, afterSetBlock + 1u );

    CHECK( !manager.setBlockOrientation( block, world::BlockOrientation::East ) );
    CHECK_EQ( notifications, afterSetBlock + 1u ); // same value again: no change

    CHECK( manager.setBlockOrientation( block, world::BlockOrientation::West ) );
    CHECK_EQ( notifications, afterSetBlock + 2u );

    CHECK( manager.setBlockOrientation( block, world::BlockOrientation::Up ) );
    CHECK_EQ( notifications, afterSetBlock + 3u ); // entry removed: real change
    CHECK( manager.chunkOrientationSidecar( world::ChunkAddress{} ) == nullptr );

    manager.clearChunkOrientations( world::ChunkAddress{} ); // nothing to clear
    CHECK_EQ( notifications, afterSetBlock + 3u );
}

TEST_CASE( sidecar_default_mod_data_is_valid )
{
    const std::filesystem::path dataDir( OMNIGRID_DATA_DIR );
    world::SidecarRegistry sidecars;
    const bool loaded = world::RegistryLoader::loadSidecars( dataDir, sidecars );

    CHECK( loaded );
    CHECK( !sidecars.empty() );

    const world::SidecarDef *orientation = sidecars.find( "core:orientation" );
    CHECK( orientation != nullptr );
    if( orientation )
    {
        CHECK( orientation->valueType == world::SidecarValueType::Uint8 );
        CHECK( std::holds_alternative<std::uint32_t>( orientation->defaultValue ) );
        CHECK_EQ( std::get<std::uint32_t>( orientation->defaultValue ), 0u );
        CHECK_EQ( orientation->bitWidth, 3u );
        CHECK( orientation->storage == world::SidecarStorageStrategy::Sparse );
        CHECK( orientation->persist );
        CHECK_EQ( orientation->serializationVersion, 1u );
    }
}

TEST_CASE( sidecar_parsing_validation )
{
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "orientation", "displayName": "O" } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "valueType": "bogus" } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "bitWidth": 0 } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "storage": "compact" } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "serializationVersion": 0 } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "unexpected": true } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O" },
        { "id": "core:orientation", "displayName": "O2" } ]})" ); } ) );

    // Type-aware defaultValue/bitWidth validation (review M04):
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:orientation", "displayName": "O", "valueType": "uint8",
          "defaultValue": 255, "bitWidth": 3 } ]})" ); } ) ); // 255 does not fit 3 bits
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:bad", "displayName": "B", "valueType": "uint8",
          "defaultValue": 256 } ]})" ); } ) ); // does not fit uint8
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:bad", "displayName": "B", "valueType": "uint8",
          "defaultValue": -1 } ]})" ); } ) ); // negative: not unsigned
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:bad", "displayName": "B", "valueType": "float", "bitWidth": 3 } ]})" ); } ) );
    CHECK( rejected( [&] { (void)parseSidecars( R"({"sidecars":[
        { "id": "core:bad", "displayName": "B", "valueType": "float",
          "defaultValue": "20.5" } ]})" ); } ) ); // string is not a number
}

TEST_CASE( sidecar_float_default_value_is_accepted )
{
    const world::SidecarRegistry sidecars = parseSidecars( R"({"sidecars":[
        { "id": "mod:heat", "displayName": "Heat", "valueType": "float", "defaultValue": 20.5 } ]})" );
    CHECK_EQ( sidecars.size(), std::size_t{ 1 } );
    const world::SidecarDef *heat = sidecars.find( "mod:heat" );
    CHECK( heat != nullptr );
    if( heat )
    {
        CHECK( heat->valueType == world::SidecarValueType::Float );
        CHECK( std::holds_alternative<float>( heat->defaultValue ) );
        CHECK_EQ( std::get<float>( heat->defaultValue ), 20.5f );
    }

    const world::SidecarRegistry integral = parseSidecars( R"({"sidecars":[
        { "id": "mod:stored", "displayName": "Stored", "valueType": "uint32",
          "defaultValue": 3000000000 } ]})" );
    const world::SidecarDef *stored = integral.find( "mod:stored" );
    CHECK( stored != nullptr );
    if( stored )
        CHECK_EQ( std::get<std::uint32_t>( stored->defaultValue ), 3000000000u );
}

TEST_CASE( sidecar_missing_file_means_no_sidecars )
{
    const std::filesystem::path emptyDir =
        std::filesystem::temp_directory_path() / "omnigrid-no-sidecars";
    std::error_code ignored;
    std::filesystem::remove_all( emptyDir, ignored );
    std::filesystem::create_directories( emptyDir );

    world::SidecarRegistry sidecars;
    const bool loaded = world::RegistryLoader::loadSidecars( emptyDir, sidecars );
    std::filesystem::remove_all( emptyDir, ignored );

    CHECK( !loaded );
    CHECK( sidecars.empty() );
}

int main() { return test::runAll(); }
