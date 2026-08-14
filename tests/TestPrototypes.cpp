#include "TestHarness.h"
#include "world/registry/ObjectRef.h"
#include "world/registry/PrototypeIdTable.h"
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

    using namespace world;

    BlockRegistry loadRealBlocks()
    {
        const std::filesystem::path dataDir( OMNIGRID_DATA_DIR );
        BlockRegistry blocks;
        BiomeRegistry biomes;
        ResourceRegistry resources;
        RegistryLoader::loadFromDirectory( dataDir, blocks, biomes, resources );
        return blocks;
    }

    SidecarRegistry loadRealSidecars()
    {
        const std::filesystem::path dataDir( OMNIGRID_DATA_DIR );
        SidecarRegistry sidecars;
        RegistryLoader::loadSidecars( dataDir, sidecars );
        return sidecars;
    }

    PrototypeRegistry parse( const std::string &text, const BlockRegistry &blocks )
    {
        PrototypeRegistry out;
        RegistryLoader::parsePrototypes( json::parse( text ), "test-prototypes.json", blocks, out );
        return out;
    }

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

    const std::string PILOT_JSON = R"({
      "prototypes": [
        {
          "id": "default:cactus",
          "displayName": "Cactus",
          "blockId": "core:cactus",
          "capabilities": ["contact.damage"]
        }
      ]
    })";
} // namespace


TEST_CASE( prototypes_default_mod_loads_pilot_cactus )
{
    const std::filesystem::path dataDir( OMNIGRID_DATA_DIR );
    const BlockRegistry blocks = loadRealBlocks();
    PrototypeRegistry prototypes;
    const bool loaded = RegistryLoader::loadPrototypes( dataDir, blocks, prototypes );

    CHECK( loaded );
    CHECK_EQ( prototypes.size(), std::size_t{ 1 } );

    const PrototypeDef *cactus = prototypes.find( "default:cactus" );
    CHECK( cactus != nullptr );
    if( cactus )
    {
        CHECK( cactus->blockId == "core:cactus" );
        CHECK( cactus->displayName == "Cactus" );
        CHECK_EQ( cactus->capabilities.size(), std::size_t{ 1 } );
        if( cactus->capabilities.size() == 1u )
            CHECK( cactus->capabilities[0] == "contact.damage" );
        // M05: the pilot prototype declares the orientation property and its
        // prototype default (Up = 0) - the unified world state resolves it.
        CHECK_EQ( cactus->properties.size(), std::size_t{ 1 } );
        if( cactus->properties.size() == 1u )
        {
            CHECK( cactus->properties[0].id == "core:orientation" );
            CHECK( std::holds_alternative<std::uint32_t>( cactus->properties[0].defaultValue ) );
            CHECK_EQ( std::get<std::uint32_t>( cactus->properties[0].defaultValue ), 0u );
        }
    }
}

TEST_CASE( prototypes_parse_properties_and_reject_invalid_declarations )
{
    const BlockRegistry blocks = loadRealBlocks();

    const PrototypeRegistry withProperties = parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [
            { "id": "core:orientation", "defaultValue": 0 },
            { "id": "mod:heat", "defaultValue": 20.5 }
          ] } ]})", blocks );
    const PrototypeDef *cactus = withProperties.find( "default:cactus" );
    CHECK( cactus != nullptr );
    if( cactus )
    {
        CHECK_EQ( cactus->properties.size(), std::size_t{ 2 } );
        if( cactus->properties.size() == 2u )
        {
            CHECK( cactus->properties[0].id == "core:orientation" );
            CHECK( std::holds_alternative<std::uint32_t>( cactus->properties[0].defaultValue ) );
            CHECK( cactus->properties[1].id == "mod:heat" );
            CHECK( std::holds_alternative<float>( cactus->properties[1].defaultValue ) );
            CHECK_EQ( std::get<float>( cactus->properties[1].defaultValue ), 20.5f );
        }
    }

    // Property without a default is invalid.
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [ { "id": "core:orientation" } ] } ]})", blocks ); } ) );
    // Non-namespaced property id.
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [ { "id": "orientation", "defaultValue": 0 } ] } ]})", blocks ); } ) );
    // Non-numeric default (string).
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [ { "id": "core:orientation", "defaultValue": "up" } ] } ]})", blocks ); } ) );
    // Duplicate property id.
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [
            { "id": "core:orientation", "defaultValue": 0 },
            { "id": "core:orientation", "defaultValue": 1 }
          ] } ]})", blocks ); } ) );
    // Unknown property field.
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [ { "id": "core:orientation", "defaultValue": 0, "unit": "deg" } ] } ]})", blocks ); } ) );
    // properties must be an array.
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": { "core:orientation": 0 } } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_reject_property_without_registered_sidecar_type )
{
    const BlockRegistry blocks = loadRealBlocks();
    const SidecarRegistry sidecars = loadRealSidecars(); // ships core:orientation

    // M05 round 2: prototype properties are validated against sidecars.json
    // at load time (ADR-027). A property with no backing sidecar type is a
    // broken mod and must be rejected, not produce has()==true later.
    PrototypeRegistry out;
    CHECK( rejected( [&] {
        RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
            { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
              "properties": [ { "id": "mod:missing", "defaultValue": 7 } ] } ]})" ),
                                         "test-prototypes.json", blocks, out, &sidecars );
    } ) );

    // Without the cross-validation gate the same document still parses (the
    // gate is opt-in for programmatic/legacy content); the runtime WorldState
    // guards has()/get()/set() consistency on that path too.
    PrototypeRegistry legacy;
    RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "properties": [ { "id": "mod:missing", "defaultValue": 7 } ] } ]})" ),
                                     "test-prototypes.json", blocks, legacy );
    CHECK_EQ( legacy.size(), std::size_t{ 1 } );
}

TEST_CASE( prototypes_reject_prototype_default_that_does_not_fit_sidecar )
{
    const BlockRegistry blocks = loadRealBlocks();
    const SidecarRegistry sidecars = loadRealSidecars(); // core:orientation: uint8, bitWidth 3

    // 255 does not fit the 3-bit orientation encoding - the M04/M05 review
    // case that used to be silently turned into the sidecar default.
    PrototypeRegistry out;
    CHECK( rejected( [&] {
        RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
            { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
              "properties": [ { "id": "core:orientation", "defaultValue": 255 } ] } ]})" ),
                                         "test-prototypes.json", blocks, out, &sidecars );
    } ) );
    // Value type mismatch: a float default for an integer sidecar type.
    CHECK( rejected( [&] {
        RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
            { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
              "properties": [ { "id": "core:orientation", "defaultValue": 0.5 } ] } ]})" ),
                                         "test-prototypes.json", blocks, out, &sidecars );
    } ) );
}

TEST_CASE( prototypes_default_mod_validates_against_default_sidecars )
{
    // The shipped content must pass the cross-validation gate: default:cactus
    // declares core:orientation with default 0, which fits the registered
    // uint8/3-bit sidecar type.
    const std::filesystem::path dataDir( OMNIGRID_DATA_DIR );
    const BlockRegistry blocks = loadRealBlocks();
    const SidecarRegistry sidecars = loadRealSidecars();
    PrototypeRegistry prototypes;
    const bool loaded = RegistryLoader::loadPrototypes( dataDir, blocks, prototypes, &sidecars );

    CHECK( loaded );
    CHECK_EQ( prototypes.size(), std::size_t{ 1 } );
    const PrototypeDef *cactus = prototypes.find( "default:cactus" );
    CHECK( cactus != nullptr );
    if( cactus )
        CHECK_EQ( cactus->properties.size(), std::size_t{ 1 } );
}

TEST_CASE( prototypes_block_bridge_resolves_pilot_block )
{
    const BlockRegistry blocks = loadRealBlocks();
    const PrototypeRegistry prototypes = parse( PILOT_JSON, blocks );

    const PrototypeDef *viaBlock = world::prototypeForBlock( prototypes, "core:cactus" );
    CHECK( viaBlock != nullptr );
    if( viaBlock )
        CHECK( viaBlock->id == "default:cactus" );

    CHECK( world::prototypeForBlock( prototypes, "core:stone" ) == nullptr );
    CHECK( world::prototypeForBlock( prototypes, "core:nonexistent" ) == nullptr );
}

TEST_CASE( prototypes_handles_are_stable_across_load_order )
{
    const BlockRegistry blocks = loadRealBlocks();
    const std::string three = R"({
      "prototypes": [
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "capabilities": ["contact.damage"] },
        { "id": "default:sand", "displayName": "Sand", "blockId": "core:sand" },
        { "id": "default:furnace", "displayName": "Furnace", "blockId": "core:stone" }
      ]
    })";
    const PrototypeRegistry a = parse( three, blocks );

    PrototypeRegistry reversed;
    {
        const json root = json::parse( R"({
          "prototypes": [
            { "id": "default:furnace", "displayName": "Furnace", "blockId": "core:stone" },
            { "id": "default:sand", "displayName": "Sand", "blockId": "core:sand" },
            { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
              "capabilities": ["contact.damage"] }
          ]
        })" );
        RegistryLoader::parsePrototypes( root, "test-prototypes.json", blocks, reversed );
    }

    const PrototypeIdTable tableA( a );
    const PrototypeIdTable tableB( reversed );
    CHECK_EQ( tableA.size(), tableB.size() );
    CHECK_EQ( tableA.size(), std::size_t{ 3 } );
    for( const std::string &id : a.ids() )
        CHECK_EQ( tableA.handleOf( id ), tableB.handleOf( id ) );
    CHECK_EQ( tableA.handleOf( "default:cactus" ),
              PrototypeIdTable::hashId( "default:cactus" ) );
}

TEST_CASE( prototypes_handle_table_strict_and_probing_lookups )
{
    const BlockRegistry blocks = loadRealBlocks();
    const PrototypeRegistry prototypes = parse( PILOT_JSON, blocks );
    const PrototypeIdTable table( prototypes );

    CHECK( table.tryHandleOf( "default:cactus" ).has_value() );
    CHECK( !table.tryHandleOf( "default:unknown" ).has_value() );
    CHECK( rejected( [&] { (void)table.handleOf( "default:unknown" ); } ) );

    const std::uint32_t handle = table.handleOf( "default:cactus" );
    CHECK( table.tryGet( handle ) != nullptr );
    CHECK( table.get( handle ).id == "default:cactus" );
    CHECK( table.tryGet( 0xDEADBEEFu ) == nullptr );
    CHECK( rejected( [&] { (void)table.get( 0xDEADBEEFu ); } ) );
}

TEST_CASE( prototypes_reject_non_namespaced_ids )
{
    const BlockRegistry blocks = loadRealBlocks();
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "cactus", "displayName": "Cactus", "blockId": "core:cactus" } ]})", blocks ); } ) );
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": ":cactus", "displayName": "Cactus", "blockId": "core:cactus" } ]})", blocks ); } ) );
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:", "displayName": "Cactus", "blockId": "core:cactus" } ]})", blocks ); } ) );
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "a:b:c", "displayName": "Cactus", "blockId": "core:cactus" } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_reject_duplicate_ids )
{
    const BlockRegistry blocks = loadRealBlocks();
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "A", "blockId": "core:cactus" },
        { "id": "default:cactus", "displayName": "B", "blockId": "core:stone" } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_reject_unknown_block_references )
{
    const BlockRegistry blocks = loadRealBlocks();
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:no_such_block" } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_reject_duplicate_capabilities_and_unknown_fields )
{
    const BlockRegistry blocks = loadRealBlocks();
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "capabilities": ["contact.damage", "contact.damage"] } ]})", blocks ); } ) );
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
          "hp": 10 } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_block_claim_is_unique )
{
    const BlockRegistry blocks = loadRealBlocks();
    CHECK( rejected( [&] { (void)parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus" },
        { "id": "default:cactus_alt", "displayName": "Other", "blockId": "core:cactus" } ]})", blocks ); } ) );
}

TEST_CASE( prototypes_block_claim_is_unique_across_parse_calls )
{
    const BlockRegistry blocks = loadRealBlocks();
    PrototypeRegistry out;

    RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
        { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus" } ]})" ),
                                     "test-prototypes-1.json", blocks, out );
    CHECK_EQ( out.size(), std::size_t{ 1 } );

    // A second, independent load call into the same registry must not be able
    // to claim core:cactus again (registry-order-dependent identity).
    CHECK( rejected( [&] {
        RegistryLoader::parsePrototypes( json::parse( R"({"prototypes":[
            { "id": "default:cactus_alt", "displayName": "Other", "blockId": "core:cactus" } ]})" ),
                                         "test-prototypes-2.json", blocks, out );
    } ) );
    CHECK_EQ( out.size(), std::size_t{ 1 } );
    CHECK( out.claimedBy( "core:cactus" ) != nullptr );
    CHECK( *out.claimedBy( "core:cactus" ) == "default:cactus" );
    CHECK( out.claimedBy( "core:stone" ) == nullptr );
}

TEST_CASE( prototypes_reject_hash_collisions )
{
    const BlockRegistry blocks = loadRealBlocks();

    const std::uint32_t a = PrototypeIdTable::hashId( "t:ptz" );
    const std::uint32_t b = PrototypeIdTable::hashId( "t:fveid" );
    CHECK_EQ( a, b );

    const PrototypeRegistry prototypes = parse( R"({"prototypes":[
        { "id": "t:ptz", "displayName": "A", "blockId": "core:stone" },
        { "id": "t:fveid", "displayName": "B", "blockId": "core:sand" } ]})", blocks );
    CHECK( rejected( [&] { const PrototypeIdTable table( prototypes ); (void)table; } ) );
}

TEST_CASE( prototypes_missing_file_means_no_prototypes )
{
    const std::filesystem::path emptyDir =
        std::filesystem::temp_directory_path() / "omnigrid-no-prototypes";
    std::error_code ignored;
    std::filesystem::remove_all( emptyDir, ignored );
    std::filesystem::create_directories( emptyDir );

    const BlockRegistry blocks = loadRealBlocks();
    PrototypeRegistry prototypes;
    const bool loaded = RegistryLoader::loadPrototypes( emptyDir, blocks, prototypes );
    std::filesystem::remove_all( emptyDir, ignored );

    CHECK( !loaded );
    CHECK( prototypes.empty() );
}

TEST_CASE( object_ref_carries_position_and_logical_identity )
{
    const BlockRegistry blocks = loadRealBlocks();
    const PrototypeRegistry prototypes = parse( PILOT_JSON, blocks );

    const world::BlockAddress position = world::fromOriginOffset( 12, 64, -3 );
    const world::WorldObjectRef ref{ position, "default:cactus" };

    CHECK( ref.position == position );
    CHECK( ref.prototypeId == "default:cactus" );

    const PrototypeDef *def = world::prototypeForBlock( prototypes, "core:cactus" );
    CHECK( def != nullptr );
    if( def )
        CHECK( ref.prototypeId == def->id );
}

int main() { return test::runAll(); }
