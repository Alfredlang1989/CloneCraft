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
    }
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
    const PrototypeRegistry a = parse( PILOT_JSON, blocks );

    PrototypeRegistry reversed;
    {
        const json root = json::parse( R"({
          "prototypes": [
            { "id": "default:cactus", "displayName": "Cactus", "blockId": "core:cactus",
              "capabilities": ["contact.damage"] }
          ]
        })" );
        RegistryLoader::parsePrototypes( root, "test-prototypes.json", blocks, reversed );
    }

    const PrototypeIdTable tableA( a );
    const PrototypeIdTable tableB( reversed );
    CHECK_EQ( tableA.size(), tableB.size() );
    CHECK_EQ( tableA.handleOf( "default:cactus" ), tableB.handleOf( "default:cactus" ) );
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
