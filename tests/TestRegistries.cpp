#include "TestHarness.h"

#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>

using namespace world;

#ifndef CLONECRAFT_DATA_DIR
#define CLONECRAFT_DATA_DIR "data"
#endif

namespace
{
    /** Runs fn and requires a RegistryError containing 'substring'. */
    void expectRegistryError( const std::function<void()> &fn, const std::string &substring )
    {
        try
        {
            fn();
            ::test::check( false, "expected RegistryError (containing '" + substring + "')",
                           __FILE__, __LINE__ );
        }
        catch( const RegistryError &e )
        {
            const std::string msg( e.what() );
            const bool contains = msg.find( substring ) != std::string::npos;
            ::test::check( contains,
                           "RegistryError message should contain '" + substring +
                               "', got: " + msg,
                           __FILE__, __LINE__ );
        }
    }

    BlockRegistry loadBlocks( const std::string &text )
    {
        BlockRegistry out;
        RegistryLoader::parseBlockFile( text, "test-blocks.json", out );
        return out;
    }

    TEST_CASE( blocks_core_data_valid )
    {
        const std::filesystem::path dataDir( CLONECRAFT_DATA_DIR );
        BlockRegistry blocks;
        BiomeRegistry biomes;
        ResourceRegistry resources;
        RegistryLoader::loadFromDirectory( dataDir, blocks, biomes, resources );

        // This is deliberately a schema/cross-reference smoke test, not a list
        // of biome ids. Adding/removing a valid data-defined biome must not
        // require editing C++ tests. Only core:air is a structural engine id.
        CHECK( !blocks.empty() );
        CHECK( !biomes.empty() );

        const BlockDef *air = blocks.find( "core:air" );
        const BlockDef *stone = blocks.find( "core:stone" );
        CHECK( air != nullptr );
        CHECK( stone != nullptr );
        if( air )
        {
            CHECK( !air->solid );
            CHECK( !air->opaque );
            CHECK( air->transparent );
            CHECK_EQ( air->displayName, std::string( "Air" ) );
            CHECK( !air->color.has_value() );
            CHECK( air->texture.empty() );
        }
        if( stone )
        {
            CHECK( stone->solid );
            CHECK( stone->opaque );
            CHECK( !stone->transparent );
            CHECK( std::find( stone->tags.begin(), stone->tags.end(), "terrain:rock" ) !=
                   stone->tags.end() );
            CHECK( std::find( stone->tags.begin(), stone->tags.end(), "terrain:carvable" ) !=
                   stone->tags.end() );
            CHECK( stone->color.has_value() );
            if( stone->color )
            {
                CHECK_EQ( stone->color->r, 138u );
                CHECK_EQ( stone->color->g, 138u );
                CHECK_EQ( stone->color->b, 140u );
                CHECK_EQ( stone->color->a, 255u );
            }
        }

        // Validate the live data generically. RegistryLoader already rejects
        // invalid references, but keeping these assertions here catches any
        // future bypass of that validation boundary.
        for( const std::string &biomeId : biomes.ids() )
        {
            const BiomeDef &biome = biomes.get( biomeId );
            CHECK( !biome.id.empty() );
            CHECK( !biome.displayName.empty() );
            CHECK( blocks.contains( biome.surfaceBlock ) );
            CHECK( blocks.contains( biome.fillerBlock ) );
            CHECK( biome.temperature >= 0.0 && biome.temperature <= 1.0 );
            CHECK( biome.rainfall >= 0.0 && biome.rainfall <= 1.0 );
            CHECK( biome.continentalness >= 0.0 && biome.continentalness <= 1.0 );
            CHECK( biome.weight > 0.0 );
            if( !biome.resourceId.empty() )
                CHECK( resources.contains( biome.resourceId ) );
        }

        for( const std::string &resourceId : resources.ids() )
        {
            const ResourceDef &resource = resources.get( resourceId );
            CHECK( !resource.id.empty() );
            CHECK( blocks.contains( resource.blockId ) );
            CHECK( resource.minY <= resource.maxY );
            CHECK( resource.chance >= 0.0 && resource.chance <= 1.0 );
        }
    }


    TEST_CASE( blocks_visuals_texture_and_color_are_data_driven )
    {
        const BlockRegistry blocks = loadBlocks( R"({ "blocks": [
            { "id": "core:tex", "displayName": "Texture",
              "texture": "textures/stone.png", "color": "#01020304" },
            { "id": "core:rgb", "displayName": "RGB", "color": [10,20,30] },
            { "id": "core:fallback", "displayName": "Fallback" }
        ] })" );

        const BlockDef &tex = blocks.get( "core:tex" );
        CHECK_EQ( tex.texture, std::string( "textures/stone.png" ) );
        CHECK( tex.color.has_value() ); // kept as fallback if file loading fails
        if( tex.color )
        {
            CHECK_EQ( tex.color->r, 1u );
            CHECK_EQ( tex.color->g, 2u );
            CHECK_EQ( tex.color->b, 3u );
            CHECK_EQ( tex.color->a, 4u );
        }

        const BlockDef &rgb = blocks.get( "core:rgb" );
        CHECK( rgb.texture.empty() );
        CHECK( rgb.color.has_value() );
        if( rgb.color )
        {
            CHECK_EQ( rgb.color->r, 10u );
            CHECK_EQ( rgb.color->g, 20u );
            CHECK_EQ( rgb.color->b, 30u );
            CHECK_EQ( rgb.color->a, 255u );
        }

        const BlockDef &fallback = blocks.get( "core:fallback" );
        CHECK( fallback.texture.empty() );
        CHECK( !fallback.color.has_value() );
    }

    TEST_CASE( blocks_render_shape_is_data_driven )
    {
        const BlockRegistry blocks = loadBlocks( R"({ "blocks": [
            { "id":"core:plant", "displayName":"Plant", "renderShape":"cross" },
            { "id":"core:cube", "displayName":"Cube", "renderShape":"cube" }
        ] })" );
        CHECK( blocks.get( "core:plant" ).renderShape == BlockRenderShape::Cross );
        CHECK( blocks.get( "core:cube" ).renderShape == BlockRenderShape::Cube );
    }

    TEST_CASE( blocks_alpha_mode_is_data_driven )
    {
        const BlockRegistry blocks = loadBlocks( R"({ "blocks": [
            { "id":"core:leaf", "displayName":"Leaf", "alphaMode":"mask",
              "alphaCutoff":0.42, "transparent":true, "opaque":true },
            { "id":"core:glass", "displayName":"Glass", "alphaMode":"blend",
              "transparency":0.35 },
            { "id":"core:stone", "displayName":"Stone" }
        ] })" );

        const BlockDef &leaf = blocks.get( "core:leaf" );
        CHECK( leaf.alphaMode == BlockAlphaMode::Mask );
        CHECK( std::fabs( leaf.alphaCutoff - 0.42f ) < 1e-5f );
        CHECK( !leaf.transparent );
        CHECK( !leaf.opaque );

        const BlockDef &glass = blocks.get( "core:glass" );
        CHECK( glass.alphaMode == BlockAlphaMode::Blend );
        CHECK( glass.transparent );
        CHECK( !glass.opaque );

        CHECK( blocks.get( "core:stone" ).alphaMode == BlockAlphaMode::Opaque );
    }

    TEST_CASE( blocks_pbr_material_fields_are_data_driven )
    {
        const BlockRegistry blocks = loadBlocks( R"({ "blocks": [
            { "id": "core:glass", "displayName": "Glass",
              "texture": "glass.png", "normalMap": "glass_n.png",
              "reflectionMap": "sky.dds", "roughness": 0.12,
              "metalness": 0.05, "reflection": 0.18, "transparency": 0.45,
              "refraction": 0.11, "indexOfRefraction": 1.52,
              "normalMapStrength": 0.75, "receiveShadows": false,
              "castShadows": false }
        ] })" );

        const BlockDef &glass = blocks.get( "core:glass" );
        CHECK_EQ( glass.normalMap, std::string( "glass_n.png" ) );
        CHECK_EQ( glass.reflectionMap, std::string( "sky.dds" ) );
        CHECK( std::fabs( glass.roughness - 0.12f ) < 1e-5f );
        CHECK( std::fabs( glass.metalness - 0.05f ) < 1e-5f );
        CHECK( std::fabs( glass.reflection - 0.18f ) < 1e-5f );
        CHECK( std::fabs( glass.transparency - 0.45f ) < 1e-5f );
        CHECK( std::fabs( glass.refraction - 0.11f ) < 1e-5f );
        CHECK( std::fabs( glass.indexOfRefraction - 1.52f ) < 1e-5f );
        CHECK( std::fabs( glass.normalMapStrength - 0.75f ) < 1e-5f );
        CHECK( !glass.receiveShadows );
        CHECK( !glass.castShadows );
        CHECK( glass.transparent ); // inferred from PBR transparency
        CHECK( !glass.opaque );
    }

    TEST_CASE( blocks_pbr_material_validation )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "roughness": 1.5 } ] })" );
        }, "roughness" );
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "transparency": -0.1 } ] })" );
        }, "transparency" );
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "normalMap": 42 } ] })" );
        }, "normalMap" );
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "alphaMode": "sparkles" } ] })" );
        }, "alphaMode" );
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "alphaMode": "mask", "alphaCutoff": 1.5 } ] })" );
        }, "alphaCutoff" );
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "alphaMode": "mask", "transparency": 0.2 } ] })" );
        }, "alphaMode 'mask'" );
    }

    TEST_CASE( blocks_color_validation )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "color": "orange" } ] })" );
        }, "#RRGGBB" );

        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X",
                                            "color": [0, 256, 0] } ] })" );
        }, "0..255" );
    }

    TEST_CASE( blocks_unknown_lookup )
    {
        const BlockRegistry blocks = loadBlocks(
            R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
        CHECK( blocks.find( "core:missing" ) == nullptr );
        CHECK( !blocks.contains( "core:missing" ) );
        CHECK( blocks.contains( "core:stone" ) );
        CHECK_EQ( blocks.size(), 1u );
        CHECK_EQ( blocks.ids().size(), 1u );
        CHECK_EQ( blocks.ids()[0], std::string( "core:stone" ) );
    }

    TEST_CASE( blocks_missing_blocks_array )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "something_else": [] })" );
        }, "blocks' array" );
    }

    TEST_CASE( blocks_entry_not_object )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ 42 ] })" );
        }, "entry 1" );
    }

    TEST_CASE( blocks_missing_required_field )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x" } ] })" );
        }, "displayName" );
    }

    TEST_CASE( blocks_field_wrong_type )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X", "solid": "yes" } ] })" );
        }, "'solid' must be a boolean" );
    }

    TEST_CASE( blocks_unknown_field )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X", "fly": true } ] })" );
        }, "unknown field 'fly'" );
    }

    TEST_CASE( blocks_emission_range )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": "core:x", "displayName": "X", "emission": 16 } ] })" );
        }, "0..15" );
    }

    TEST_CASE( blocks_duplicate_id )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [
                { "id": "core:stone", "displayName": "Stone A" },
                { "id": "core:stone", "displayName": "Stone B" }
            ] })" );
        }, "duplicate id 'core:stone'" );
    }

    TEST_CASE( blocks_bad_json_syntax )
    {
        expectRegistryError( [] {
            loadBlocks( R"({ "blocks": [ { "id": )" );
        }, "JSON parse error" );
    }

    TEST_CASE( blocks_missing_file_tagged_in_message )
    {
        expectRegistryError( [] {
            BlockRegistry out;
            RegistryLoader::parseBlockFile( "not json", "test-blocks.json", out );
        }, "test-blocks.json" );
    }

    TEST_CASE( biomes_optional_filler_defaults_to_surface )
    {
        const BlockRegistry blocks = loadBlocks(
            R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
        BiomeRegistry biomes;
        RegistryLoader::parseBiomes(
            nlohmann::json::parse( R"({ "biomes": [
                { "id": "core:test", "displayName": "Test", "surfaceBlock": "core:stone" }
            ] })" ),
            "test-biomes.json", biomes, blocks );
        CHECK_EQ( biomes.get( "core:test" ).fillerBlock, std::string( "core:stone" ) );
    }

    TEST_CASE( biome_terrain_detail_shape_is_data_driven )
    {
        const BlockRegistry blocks = loadBlocks(
            R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
        BiomeRegistry biomes;
        RegistryLoader::parseBiomes(
            nlohmann::json::parse( R"({ "biomes": [
                { "id": "core:test", "displayName": "Test", "surfaceBlock": "core:stone",
                  "terrain": { "heightMultiplier": 1.25,
                               "detailAmplitude": 7.5,
                               "detailScale": 0.031,
                               "detailMultiplier": 0.4 } }
            ] })" ),
            "test-biomes.json", biomes, blocks );

        const BiomeTerrainDef &terrain = biomes.get( "core:test" ).terrain;
        CHECK( std::fabs( terrain.heightMultiplier - 1.25 ) < 1e-9 );
        CHECK( std::fabs( terrain.detailAmplitude - 7.5 ) < 1e-9 );
        CHECK( std::fabs( terrain.detailScale - 0.031 ) < 1e-9 );
        CHECK( std::fabs( terrain.detailMultiplier - 0.4 ) < 1e-9 );
    }

    TEST_CASE( biome_terrain_detail_scale_validation )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            BiomeRegistry biomes;
            RegistryLoader::parseBiomes(
                nlohmann::json::parse( R"({ "biomes": [
                    { "id": "core:test", "displayName": "Test", "surfaceBlock": "core:stone",
                      "terrain": { "detailScale": 0.0 } }
                ] })" ),
                "test-biomes.json", biomes, blocks );
        }, "terrain scales" );
    }

    TEST_CASE( biomes_climate_values_are_normalized_range )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            BiomeRegistry biomes;
            RegistryLoader::parseBiomes(
                nlohmann::json::parse( R"({ "biomes": [
                    { "id": "core:test", "displayName": "Test", "surfaceBlock": "core:stone",
                      "temperature": 1.5 }
                ] })" ),
                "test-biomes.json", biomes, blocks );
        }, "temperature' must be in 0..1" );
    }

    TEST_CASE( biomes_unknown_surface_block )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            BiomeRegistry biomes;
            RegistryLoader::parseBiomes(
                nlohmann::json::parse( R"({ "biomes": [
                    { "id": "core:pl", "displayName": "P", "surfaceBlock": "core:ghost" }
                ] })" ),
                "test-biomes.json", biomes, blocks );
        }, "surfaceBlock 'core:ghost' is not a registered block" );
    }

    TEST_CASE( resources_unknown_block )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            ResourceRegistry resources;
            RegistryLoader::parseResources(
                nlohmann::json::parse( R"({ "resources": [
                    { "id": "core:r", "displayName": "R", "blockId": "core:ghost" }
                ] })" ),
                "test-resources.json", resources, blocks );
        }, "blockId 'core:ghost' is not a registered block" );
    }

    TEST_CASE( resources_minY_above_maxY )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            ResourceRegistry resources;
            RegistryLoader::parseResources(
                nlohmann::json::parse( R"({ "resources": [
                    { "id": "r", "displayName": "R", "blockId": "core:stone", "minY": 99, "maxY": 1 }
                ] })" ),
                "test-resources.json", resources, blocks );
        }, "minY' > 'maxY'" );
    }
    TEST_CASE( block_defaults_do_not_claim_transparent_and_opaque_at_once )
    {
        const BlockRegistry blocks = loadBlocks(
            R"({ "blocks": [ { "id": "core:plain", "displayName": "Plain" } ] })" );
        const BlockDef &plain = blocks.get( "core:plain" );
        CHECK( !plain.transparent );
        CHECK( plain.opaque );
    }

    TEST_CASE( resources_chance_is_validated )
    {
        expectRegistryError( [] {
            const BlockRegistry blocks = loadBlocks(
                R"({ "blocks": [ { "id": "core:stone", "displayName": "Stone" } ] })" );
            ResourceRegistry resources;
            RegistryLoader::parseResources(
                nlohmann::json::parse( R"({ "resources": [
                    { "id": "core:r", "displayName": "R", "blockId": "core:stone", "chance": 1.2 }
                ] })" ),
                "test-resources.json", resources, blocks );
        }, "chance' must be in 0..1" );
    }

} // namespace

int main() { return test::runAll(); }