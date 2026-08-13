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
    world::OrientationSidecar sidecar;
    sidecar.set( 5u, world::BlockOrientation::East );
    CHECK( !sidecar.empty() );
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 1 } );
    CHECK( sidecar.get( 5u ) == world::BlockOrientation::East );

    sidecar.set( 4096u, world::BlockOrientation::South );
    CHECK_EQ( sidecar.entryCount(), std::size_t{ 2 } );
    CHECK( sidecar.get( 5u ) == world::BlockOrientation::East );
    CHECK( sidecar.get( 4096u ) == world::BlockOrientation::South );
    CHECK( !sidecar.get( 6u ).has_value() );
}

TEST_CASE( sidecar_lazy_destruction_when_last_entry_returns_to_default )
{
    world::OrientationSidecar sidecar;
    sidecar.set( 5u, world::BlockOrientation::North );
    CHECK( !sidecar.empty() );

    sidecar.set( 5u, world::BlockOrientation::Up );
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
    CHECK( chunk.orientationSidecar() == nullptr );
    CHECK( !chunk.blockOrientation( 1, 2, 3 ).has_value() );

    chunk.setBlockOrientation( 1, 2, 3, world::BlockOrientation::East );
    CHECK( chunk.orientationSidecar() != nullptr );
    CHECK( chunk.blockOrientation( 1, 2, 3 ) == world::BlockOrientation::East );
    CHECK( !chunk.blockOrientation( 1, 2, 4 ).has_value() );
}

TEST_CASE( chunk_sidecar_disappears_again_after_last_default_write )
{
    world::Chunk chunk( world::ChunkAddress{} );
    chunk.setBlockOrientation( 1, 2, 3, world::BlockOrientation::East );
    chunk.setBlockOrientation( 4, 5, 6, world::BlockOrientation::South );
    CHECK( chunk.orientationSidecar() != nullptr );

    chunk.setBlockOrientation( 1, 2, 3, world::BlockOrientation::Up );
    CHECK( chunk.orientationSidecar() != nullptr ); // one entry left

    chunk.setBlockOrientation( 4, 5, 6, world::BlockOrientation::Up );
    CHECK( chunk.orientationSidecar() == nullptr ); // sidecar dropped again

    chunk.setBlockOrientation( 4, 5, 6, world::BlockOrientation::South );
    chunk.clearOrientations();
    CHECK( chunk.orientationSidecar() == nullptr );
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
    manager.setBlockOrientation( block, world::BlockOrientation::East );
    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !manager.blockOrientation( block ).has_value() );
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
        CHECK_EQ( orientation->defaultValue, 0u );
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
