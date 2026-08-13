#include "TestHarness.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/OrientationSidecar.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace
{
    using json = nlohmann::json;

    world::SidecarRegistry parseSidecars( const std::string &text )
    {
        world::SidecarRegistry out;
        world::RegistryLoader::parseSidecars( json::parse( text ), "test-sidecars.json", out );
        return out;
    }

    world::SidecarRegistry pilotSidecars()
    {
        // Mirrors MODS/Default/sidecars.json for "core:orientation".
        return parseSidecars( R"({"sidecars":[
            { "id": "core:orientation", "displayName": "Orientation", "valueType": "uint8",
              "defaultValue": 0, "bitWidth": 3, "storage": "sparse" } ]})" );
    }

    const world::BlockAddress B1 = world::fromOriginOffset( 3, 7, 9 );
    const world::BlockAddress B2 = world::fromOriginOffset( 4, 7, 9 );
    const world::PropertyValue UP{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue EAST{ world::blockOrientationValue( world::BlockOrientation::East ) };
    const world::PropertyValue WEST{ world::blockOrientationValue( world::BlockOrientation::West ) };
} // namespace


TEST_CASE( world_state_get_returns_declared_default_without_stored_state )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    // No chunk loaded, nothing stored: the resolver still answers with the
    // data-driven default (M05: caller must not care where the value lives).
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) ); // no explicit entry
}

TEST_CASE( world_state_get_rejects_unknown_property_ids )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    CHECK( !state.get( B1, "mod:nope" ).has_value() );
    CHECK( !state.has( B1, "mod:nope" ) );
}

TEST_CASE( world_state_set_and_get_roundtrip_with_default_removal )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    CHECK( state.setBlock( B1, 7u ) );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );

    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == EAST );

    // Writing the declared default removes the stored entry again.
    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
}

TEST_CASE( world_state_set_rejects_type_mismatch )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    CHECK( state.setBlock( B1, 7u ) );
    // core:orientation is an integral type; floats must be rejected.
    CHECK( !state.set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 0.5f } ) );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
}

TEST_CASE( world_state_set_rejects_unknown_property_ids )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    CHECK( state.setBlock( B1, 7u ) );
    CHECK( !state.set( B1, "mod:nope", UP ) );
}

TEST_CASE( world_state_set_rejects_air_and_never_creates_chunks )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) ); // AIR block
    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );

    // Absent chunk (no block set): still no chunk creation.
    CHECK( !state.set( B2, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( manager.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_set_noop_does_not_notify_or_persist )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );
    world::MemoryPersistenceSink sink;
    state.setPersistenceSink( &sink );

    std::vector<std::pair<world::BlockAddress, std::string>> events;
    state.setOnChange( [&]( const world::BlockAddress &addr, const std::string &what )
                       { events.emplace_back( addr, what ); } );

    CHECK( state.setBlock( B1, 7u ) ); // real change: fires + persists
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( events[0].second, "block" );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );

    CHECK( !state.setBlock( B1, 7u ) ); // same id: no-op
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );

    CHECK( !state.set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) ); // default on default
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_change_hook_fires_for_real_property_changes )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    std::vector<std::string> whats;
    state.setOnChange( [&]( const world::BlockAddress &, const std::string &what )
                       { whats.push_back( what ); } );

    state.setBlock( B1, 7u );
    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, WEST ) );
    CHECK( !state.set( B1, world::CORE_ORIENTATION_SIDECAR, WEST ) ); // no-op

    CHECK_EQ( whats.size(), std::size_t{ 3 } ); // block + east + west
    CHECK_EQ( whats[0], "block" );
    CHECK_EQ( whats[1], world::CORE_ORIENTATION_SIDECAR );
    CHECK_EQ( whats[2], world::CORE_ORIENTATION_SIDECAR );
}

TEST_CASE( world_state_set_block_is_central_mutation_with_neighbor_notification )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    std::size_t chunkNotifications = 0u;
    manager.setOnChunkChange( [&]( const world::ChunkAddress & ) { ++chunkNotifications; } );

    CHECK( state.setBlock( B1, 5u ) );
    CHECK( chunkNotifications > 0u ); // chunk + boundary neighbors notified

    CHECK( state.blockAt( B1 ) == std::optional<std::uint16_t>{ 5u } );
    CHECK( !state.setBlock( B1, 5u ) ); // no-op
}

TEST_CASE( world_state_persistence_sink_records_block_and_property_changes )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );
    world::MemoryPersistenceSink sink;
    state.setPersistenceSink( &sink );

    state.setBlock( B1, 7u );
    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 1 } );
    CHECK_EQ( sink.dirtyChunkCount(), std::size_t{ 1 } );
    CHECK( sink.isDirty( B1.chunk ) );
    CHECK_EQ( sink.blockDeltas().at( B1 ).oldRuntimeId, 0u );
    CHECK_EQ( sink.blockDeltas().at( B1 ).newRuntimeId, 7u );

    state.setBlock( B1, 9u ); // overwrite: last-write-wins
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );
    CHECK_EQ( sink.blockDeltas().at( B1 ).oldRuntimeId, 7u );
    CHECK_EQ( sink.blockDeltas().at( B1 ).newRuntimeId, 9u );

    sink.flush();
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 0 } );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );
    CHECK_EQ( sink.dirtyChunkCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_orientation_pilot_is_consistent_with_manager_shim )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );

    // One storage proof: the manager orientation shim and the unified world
    // state read and write the exact same sidecar state.
    CHECK( state.setBlock( B1, 7u ) );
    CHECK( manager.setBlockOrientation( B1, world::BlockOrientation::West ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == WEST );
    CHECK( state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( manager.blockOrientation( B1 ) == world::BlockOrientation::West );

    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( manager.blockOrientation( B1 ) == world::BlockOrientation::East );

    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) );
    CHECK( !manager.blockOrientation( B1 ).has_value() );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
}

TEST_CASE( world_state_orientation_is_cleared_when_block_is_removed )
{
    world::ChunkManager manager;
    const world::SidecarRegistry sidecars = pilotSidecars();
    world::WorldState state( manager, sidecars );
    world::MemoryPersistenceSink sink;
    state.setPersistenceSink( &sink );

    state.setBlock( B1, 7u );
    CHECK( state.set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );

    // Removing the block invalidates its sidecar state (no zombies).
    CHECK( state.setBlock( B1, 0u ) );
    CHECK( !state.has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( state.get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
    CHECK( !manager.blockOrientation( B1 ).has_value() );
    CHECK_EQ( sink.blockDeltas().at( B1 ).newRuntimeId, 0u );
}

int main() { return test::runAll(); }