#include "TestHarness.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/OrientationSidecar.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"

#include <nlohmann/json.hpp>

#include <memory>
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

    const std::string PILOT_SIDECARS = R"({"sidecars":[
        { "id": "core:orientation", "displayName": "Orientation", "scope": "block", "valueType": "uint8",
          "defaultValue": 0, "bitWidth": 3, "storage": "sparse" } ]})";

    const std::string ORIENTED_PROPERTIES =
        R"([ { "id": "core:orientation", "defaultValue": 0 } ])";

    const world::BlockAddress B1 = world::fromOriginOffset( 3, 7, 9 );   // interior
    const world::BlockAddress BOUNDARY = world::fromOriginOffset( 0, 7, 9 ); // x = 0 chunk edge
    const world::PropertyValue UP{ world::blockOrientationValue( world::BlockOrientation::Up ) };
    const world::PropertyValue EAST{ world::blockOrientationValue( world::BlockOrientation::East ) };
    const world::PropertyValue WEST{ world::blockOrientationValue( world::BlockOrientation::West ) };

    world::BlockDef makeBlock( const std::string &id, const std::string &displayName )
    {
        world::BlockDef def;
        def.id = id;
        def.displayName = displayName;
        return def;
    }

    /**
     * Test world: blocks core:air / test:oriented / test:plain; the prototype
     * "test:oriented" (blockId test:oriented) declares the given properties;
     * "test:plain" has no prototype (pure scenery). The unified world state
     * therefore answers properties only for oriented blocks.
     */
    struct Fixture
    {
        world::BlockRegistry blocks;
        world::BlockIdTable idTable;
        world::PrototypeRegistry prototypes;
        world::SidecarRegistry sidecars;
        world::ChunkManager manager;
        std::unique_ptr<world::WorldState> state;

        std::uint16_t orientedId;
        std::uint16_t plainId;

        explicit Fixture( const std::string &sidecarsJson = PILOT_SIDECARS,
                          const std::string &propertiesJson = ORIENTED_PROPERTIES )
        {
            blocks.insert( makeBlock( "core:air", "Air" ) );
            blocks.insert( makeBlock( "test:oriented", "Oriented" ) );
            blocks.insert( makeBlock( "test:plain", "Plain" ) );
            idTable = world::BlockIdTable( blocks );
            orientedId = idTable.indexOf( "test:oriented" );
            plainId = idTable.indexOf( "test:plain" );

            // Sidecars are parsed before prototypes so the prototype property
            // declarations are validated against them at load time (ADR-027).
            sidecars = parseSidecars( sidecarsJson );
            const json prototypeJson = json::parse(
                R"({"prototypes":[
                    { "id": "test:oriented", "displayName": "Oriented", "blockId": "test:oriented",
                      "properties": )" + propertiesJson + R"( }
                ]})" );
            world::RegistryLoader::parsePrototypes( prototypeJson, "test-prototypes.json",
                                                    blocks, prototypes, &sidecars );
            state = std::make_unique<world::WorldState>( manager, idTable, sidecars, prototypes );
        }
    };
} // namespace


TEST_CASE( world_state_unloaded_and_air_positions_own_no_properties )
{
    Fixture f;
    // Unloaded chunk: no object, no property.
    CHECK( !f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( !f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ).has_value() );
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_plain_block_without_prototype_has_no_properties )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.plainId ) ); // loaded block, but pure scenery
    CHECK( !f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( !f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ).has_value() );
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
}

TEST_CASE( world_state_get_resolves_prototype_default )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
}

TEST_CASE( world_state_prototype_default_wins_over_sidecar_default )
{
    // Prototype default 20.5 differs from the sidecar type default 15.0.
    const std::string sidecars = R"({"sidecars":[
        { "id": "mod:heat", "displayName": "Heat", "scope": "block", "valueType": "float", "defaultValue": 15.0 } ]})";
    const std::string properties = R"([ { "id": "mod:heat", "defaultValue": 20.5 } ])";
    Fixture f( sidecars, properties );

    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->has( B1, "mod:heat" ) );
    const std::optional<world::PropertyValue> value = f.state->get( B1, "mod:heat" );
    CHECK( value.has_value() );
    if( value )
    {
        CHECK( std::holds_alternative<float>( *value ) );
        CHECK_EQ( std::get<float>( *value ), 20.5f );
    }
}

TEST_CASE( world_state_get_prefers_stored_override_over_prototype_default )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == EAST );
}

TEST_CASE( world_state_has_reports_logical_capability_not_stored_state )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.orientedId ) );
    // has() answers "does this object support the property", not "is an
    // explicit entry stored": it stays true after writing the default.
    CHECK( f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) );
    CHECK( f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
}

TEST_CASE( world_state_unknown_and_undeclared_properties_are_rejected )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.orientedId ) );

    // Unknown property id: rejected everywhere.
    CHECK( !f.state->get( B1, "mod:nope" ).has_value() );
    CHECK( !f.state->has( B1, "mod:nope" ) );
    CHECK( !f.state->set( B1, "mod:nope", UP ) );

    // Property declared in sidecars.json but not by the prototype: rejected.
    CHECK( !f.state->has( B1, "core:unknown-to-prototype" ) );
    CHECK( !f.state->set( B1, "core:unknown-to-prototype", UP ) );
}

TEST_CASE( world_state_set_validates_runtime_value_against_sidecar_type )
{
    Fixture f; // core:orientation: uint8, bitWidth 3
    CHECK( f.state->setBlock( B1, f.orientedId ) );

    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 0.5f } ) );
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 255u } ) );
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 8u } ) ); // >3 bits
    // 6 fits the declared 3 bits; the physical orientation enum (0..5) is
    // BlockOrientation semantics, not part of the generic type gate.
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 6u } ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 7u } ) ); // max 3-bit value fits
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, world::PropertyValue{ 5u } ) ); // West
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == WEST );
}

TEST_CASE( world_state_set_rejects_air_and_never_creates_chunks )
{
    Fixture f;
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) ); // AIR, no chunk yet
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !f.state->set( BOUNDARY, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_noop_does_not_notify_or_persist )
{
    Fixture f;
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );

    std::vector<std::pair<world::BlockAddress, std::string>> events;
    f.state->setOnChange( [&]( const world::BlockAddress &addr, const std::string &what )
                          { events.emplace_back( addr, what ); } );

    CHECK( f.state->setBlock( B1, f.orientedId ) ); // real change: fires + persists
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( events[0].second, "block" );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );

    CHECK( !f.state->setBlock( B1, f.orientedId ) ); // same id: no-op
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } );

    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) ); // default on default
    CHECK_EQ( events.size(), std::size_t{ 1 } );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_change_hook_fires_for_real_property_changes )
{
    Fixture f;
    std::vector<std::string> whats;
    f.state->setOnChange( [&]( const world::BlockAddress &, const std::string &what )
                          { whats.push_back( what ); } );

    f.state->setBlock( B1, f.orientedId );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, WEST ) );
    CHECK( !f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, WEST ) ); // no-op

    CHECK_EQ( whats.size(), std::size_t{ 3 } ); // block + east + west
    CHECK_EQ( whats[0], "block" );
    CHECK_EQ( whats[1], world::CORE_ORIENTATION_SIDECAR );
    CHECK_EQ( whats[2], world::CORE_ORIENTATION_SIDECAR );
}

TEST_CASE( world_state_set_block_is_central_mutation_with_chunk_notification )
{
    Fixture f;
    std::size_t chunkNotifications = 0u;
    f.manager.setOnChunkChange( [&]( const world::ChunkAddress & ) { ++chunkNotifications; } );

    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( chunkNotifications > 0u ); // chunk notified; interior block: no neighbour
    CHECK( f.state->blockAt( B1 ) == std::optional<std::uint16_t>{ f.orientedId } );
    CHECK( !f.state->setBlock( B1, f.orientedId ) ); // no-op
}

TEST_CASE( world_state_property_change_invalidates_boundary_neighbors )
{
    Fixture f;
    std::size_t chunkNotifications = 0u;
    f.manager.setOnChunkChange( [&]( const world::ChunkAddress & ) { ++chunkNotifications; } );

    // Boundary block (x = 0): both the own chunk and the x-1 neighbour are
    // invalidated - for the block and for a property on that block (M05
    // mesh/neighbour invalidation).
    CHECK( f.state->setBlock( BOUNDARY, f.orientedId ) );
    CHECK_EQ( chunkNotifications, std::size_t{ 2 } );

    CHECK( f.state->set( BOUNDARY, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( chunkNotifications, std::size_t{ 4 } );

    CHECK( !f.state->set( BOUNDARY, world::CORE_ORIENTATION_SIDECAR, EAST ) ); // no-op
    CHECK_EQ( chunkNotifications, std::size_t{ 4 } );
}

TEST_CASE( world_state_persist_false_properties_never_reach_sink )
{
    const std::string sidecars = R"({"sidecars":[
        { "id": "core:orientation", "displayName": "Orientation", "scope": "block", "valueType": "uint8",
          "defaultValue": 0, "bitWidth": 3, "storage": "sparse" },
        { "id": "core:transient", "displayName": "Transient", "scope": "block", "valueType": "uint8",
          "defaultValue": 0, "persist": false } ]})";
    const std::string properties =
        R"([ { "id": "core:orientation", "defaultValue": 0 },
             { "id": "core:transient", "defaultValue": 0 } ])";
    Fixture f( sidecars, properties );
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );

    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->set( B1, "core:transient", world::PropertyValue{ 5u } ) ); // changed, but not persistable
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );

    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) ); // persist: true
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 1 } );
}

TEST_CASE( world_state_property_delta_carries_value_and_removal )
{
    Fixture f;
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );

    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 1 } );
    CHECK( sink.propertyDeltas().at( { B1, world::CORE_ORIENTATION_SIDECAR } ).value == EAST );

    // Writing the prototype default removes the override: the sink learns the
    // property no longer has explicit state (real delta, not a bare marker).
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 1 } ); // last-write-wins
    CHECK( !sink.propertyDeltas().at( { B1, world::CORE_ORIENTATION_SIDECAR } ).value.has_value() );

    // Replacing the block invalidates its property override as well.
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.state->setBlock( B1, f.orientedId == 0u ? 1u : 0u ) ); // replace by AIR
    CHECK( !sink.propertyDeltas().at( { B1, world::CORE_ORIENTATION_SIDECAR } ).value.has_value() );
    CHECK_EQ( sink.blockDeltas().at( B1 ).newRuntimeId, 0u );

    sink.flush();
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 0 } );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );
    CHECK_EQ( sink.dirtyChunkCount(), std::size_t{ 0 } );
}

TEST_CASE( world_state_orientation_pilot_is_consistent_with_manager_shim )
{
    Fixture f;
    CHECK( f.state->setBlock( B1, f.orientedId ) );

    // One storage proof: the manager orientation shim and the unified world
    // state read and write the exact same sidecar state.
    CHECK( f.manager.setBlockOrientation( B1, world::BlockOrientation::West ) );
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == WEST );
    CHECK( f.manager.blockOrientation( B1 ) == world::BlockOrientation::West );

    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.manager.blockOrientation( B1 ) == world::BlockOrientation::East );

    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, UP ) );
    CHECK( !f.manager.blockOrientation( B1 ).has_value() );
    CHECK( f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) ); // capability stays
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == UP );
}

TEST_CASE( world_state_orientation_is_cleared_when_block_is_removed )
{
    Fixture f;
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );

    f.state->setBlock( B1, f.orientedId );
    CHECK( f.state->set( B1, world::CORE_ORIENTATION_SIDECAR, EAST ) );
    CHECK( f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ) == EAST );

    // Removing the block invalidates its sidecar state (no zombies): the
    // position becomes AIR, which owns no properties at all.
    CHECK( f.state->setBlock( B1, 0u ) );
    CHECK( !f.state->has( B1, world::CORE_ORIENTATION_SIDECAR ) );
    CHECK( !f.state->get( B1, world::CORE_ORIENTATION_SIDECAR ).has_value() );
    CHECK( !f.manager.blockOrientation( B1 ).has_value() );
    CHECK_EQ( sink.blockDeltas().at( B1 ).newRuntimeId, 0u );
}

TEST_CASE( world_state_shared_property_defaults_are_write_order_independent )
{
    // Two prototypes share sidecar "mod:p" inside one chunk with different
    // logical defaults (A = 0, B = 1). The sidecar's remove-again decision
    // must use the object's own logical default per write, never a
    // chunk-wide baked default: B creating the sidecar first must not make
    // A's value 1 "disappear" (M05 review round 2, HIGH).
    const std::string sidecars = R"({"sidecars":[
        { "id": "mod:p", "displayName": "P", "scope": "block", "valueType": "uint8", "defaultValue": 0 } ]})";

    const auto build = [&]() {
        struct World
        {
            world::BlockRegistry blocks;
            world::BlockIdTable idTable;
            world::PrototypeRegistry prototypes;
            world::SidecarRegistry sidecars;
            world::ChunkManager manager;
            std::unique_ptr<world::WorldState> state;
            std::uint16_t idA = 0u;
            std::uint16_t idB = 0u;
        } w;
        w.blocks.insert( makeBlock( "core:air", "Air" ) );
        w.blocks.insert( makeBlock( "test:a", "A" ) );
        w.blocks.insert( makeBlock( "test:b", "B" ) );
        w.idTable = world::BlockIdTable( w.blocks );
        w.idA = w.idTable.indexOf( "test:a" );
        w.idB = w.idTable.indexOf( "test:b" );
        w.sidecars = parseSidecars( sidecars );
        const json prototypeJson = json::parse(
            R"({"prototypes":[
                { "id": "test:a", "displayName": "A", "blockId": "test:a",
                  "properties": [ { "id": "mod:p", "defaultValue": 0 } ] },
                { "id": "test:b", "displayName": "B", "blockId": "test:b",
                  "properties": [ { "id": "mod:p", "defaultValue": 1 } ] }
            ]})" );
        world::RegistryLoader::parsePrototypes( prototypeJson, "test-prototypes.json",
                                                w.blocks, w.prototypes, &w.sidecars );
        w.state =
            std::make_unique<world::WorldState>( w.manager, w.idTable, w.sidecars, w.prototypes );
        return w;
    };

    // Order 1: B creates the sidecar first (a chunk-wide baked default would
    // be B's 1, the exact trap the review reproduced).
    {
        auto w = build();
        const world::BlockAddress a = world::fromOriginOffset( 3, 7, 9 );
        const world::BlockAddress b = world::fromOriginOffset( 4, 7, 9 );
        CHECK( w.state->setBlock( a, w.idA ) );
        CHECK( w.state->setBlock( b, w.idB ) );
        CHECK( w.state->set( b, "mod:p", world::PropertyValue{ 2u } ) ); // B override
        CHECK( w.state->set( a, "mod:p", world::PropertyValue{ 1u } ) ); // 1 != A default 0
        CHECK( w.state->get( a, "mod:p" ) == world::PropertyValue{ 1u } ); // NOT lost
        CHECK( w.state->get( b, "mod:p" ) == world::PropertyValue{ 2u } );
        // Writing each object's own logical default removes exactly its override.
        CHECK( w.state->set( a, "mod:p", world::PropertyValue{ 0u } ) );
        CHECK( w.state->get( a, "mod:p" ) == world::PropertyValue{ 0u } );
        CHECK( w.state->set( b, "mod:p", world::PropertyValue{ 1u } ) );
        CHECK( w.state->get( b, "mod:p" ) == world::PropertyValue{ 1u } );
    }

    // Order 2: A creates the sidecar first. Same final behaviour.
    {
        auto w = build();
        const world::BlockAddress a = world::fromOriginOffset( 3, 7, 9 );
        const world::BlockAddress b = world::fromOriginOffset( 4, 7, 9 );
        CHECK( w.state->setBlock( a, w.idA ) );
        CHECK( w.state->setBlock( b, w.idB ) );
        CHECK( w.state->set( a, "mod:p", world::PropertyValue{ 2u } ) ); // A override
        CHECK( w.state->set( b, "mod:p", world::PropertyValue{ 3u } ) ); // B override
        CHECK( w.state->set( a, "mod:p", world::PropertyValue{ 1u } ) ); // A override 1 kept
        CHECK( w.state->get( a, "mod:p" ) == world::PropertyValue{ 1u } );
        CHECK( w.state->get( b, "mod:p" ) == world::PropertyValue{ 3u } );
        CHECK( w.state->set( a, "mod:p", world::PropertyValue{ 0u } ) ); // A default: removed
        CHECK( w.state->get( b, "mod:p" ) == world::PropertyValue{ 3u } ); // B unaffected
    }
}

TEST_CASE( world_state_persist_false_property_is_not_reported_when_block_is_replaced )
{
    const std::string sidecars = R"({"sidecars":[
        { "id": "core:orientation", "displayName": "Orientation", "scope": "block", "valueType": "uint8",
          "defaultValue": 0, "bitWidth": 3, "storage": "sparse" },
        { "id": "core:transient", "displayName": "Transient", "scope": "block", "valueType": "uint8",
          "defaultValue": 0, "persist": false } ]})";
    const std::string properties =
        R"([ { "id": "core:orientation", "defaultValue": 0 },
             { "id": "core:transient", "defaultValue": 0 } ])";
    Fixture f( sidecars, properties );
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );

    CHECK( f.state->setBlock( B1, f.orientedId ) );
    CHECK( f.state->set( B1, "core:transient", world::PropertyValue{ 5u } ) );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } ); // persist:false: never reaches sink

    // Replacing the block must not suddenly leak a persist:false removal
    // delta (M05 review round 2: the normal set() path was fixed, the block
    // replacement path was not).
    CHECK( f.state->setBlock( B1, 0u ) );
    CHECK_EQ( sink.propertyDeltaCount(), std::size_t{ 0 } );
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 1 } ); // only the block delta
}

TEST_CASE( world_state_set_block_rejects_invalid_runtime_ids )
{
    Fixture f;
    world::MemoryPersistenceSink sink;
    f.state->setPersistenceSink( &sink );
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } );

    CHECK( !f.state->setBlock( B1, static_cast<std::uint16_t>( 0xFFFFu ) ) );
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } ); // rejected: nothing materialized
    CHECK_EQ( sink.blockDeltaCount(), std::size_t{ 0 } );
    CHECK( !f.state->blockAt( B1 ).has_value() );
}

TEST_CASE( world_state_air_noop_on_unloaded_position_never_creates_chunks )
{
    Fixture f;
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } );
    CHECK( !f.state->setBlock( B1, 0u ) ); // AIR on unloaded: vacuous no-op
    CHECK_EQ( f.manager.chunkCount(), std::size_t{ 0 } ); // no empty-chunk graveyard
}

TEST_CASE( world_state_prototype_declaring_missing_sidecar_has_no_capability )
{
    // Content loaded without the sidecar cross-validation gate (e.g. a
    // programmatic registry) must not produce has()/get()/set() schizophrenia:
    // has() refuses a property id with no registered sidecar type (M05
    // review round 2). The Application always validates, this guards the
    // API for any other construction path.
    world::BlockRegistry blocks;
    blocks.insert( makeBlock( "core:air", "Air" ) );
    blocks.insert( makeBlock( "test:oriented", "Oriented" ) );
    const world::BlockIdTable idTable( blocks );
    world::PrototypeRegistry prototypes;
    const json prototypeJson = json::parse(
        R"({"prototypes":[
            { "id": "test:oriented", "displayName": "Oriented", "blockId": "test:oriented",
              "properties": [ { "id": "core:ghost", "defaultValue": 0 } ] }
        ]})" );
    world::RegistryLoader::parsePrototypes( prototypeJson, "test-prototypes.json",
                                            blocks, prototypes ); // no sidecars gate
    world::SidecarRegistry sidecars; // empty: core:ghost resolves to nothing
    world::ChunkManager manager;
    world::WorldState state( manager, idTable, sidecars, prototypes );

    CHECK( state.setBlock( B1, idTable.indexOf( "test:oriented" ) ) );
    CHECK( !state.has( B1, "core:ghost" ) );
    CHECK( !state.get( B1, "core:ghost" ).has_value() );
    CHECK( !state.set( B1, "core:ghost", world::PropertyValue{ 0u } ) );
}

int main() { return test::runAll(); }
