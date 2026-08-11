#include "TestHarness.h"

#include "world/worldgen/WorldGenConfigLoader.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path writeTempConfig(
        const std::string &name, const std::string &text,
        const std::string &stages = R"({"stages":[{"id":"terrain","order":0},{"id":"addon","order":1}]})" )
    {
        const auto dir = std::filesystem::temp_directory_path() / ( name + "-dir" );
        std::filesystem::remove_all( dir );
        std::filesystem::create_directories( dir );
        const auto path = dir / "worldgen.json";
        std::ofstream out( path );
        out << text;
        std::ofstream stageOut( dir / "stage.json" );
        stageOut << stages;
        return path;
    }

    void removeTempConfig( const std::filesystem::path &path )
    {
        std::filesystem::remove_all( path.parent_path() );
    }

    TEST_CASE( worldgen_config_loads_fields_and_generic_passes )
    {
        const auto path = writeTempConfig(
            "clonecraft-worldgen-test.json",
            R"({
                "seed": 42,
                "workerThreads": 6,
                "surfaceField": "height",
                "fields": [
                    { "id":"height", "dimension":"2d", "script":"height.lua", "salt":11 },
                    { "id":"mask", "dimension":"2d", "script":"mask.lua", "salt":12 },
                    { "id":"ore", "dimension":"3d", "script":"ore.lua", "function":"sampleOre" }
                ],
                "passes": [
                    { "id":"stone", "type":"fill_below", "stage":"terrain", "block":"core:stone",
                      "priority":0, "field":"height" },
                    { "id":"sandstone", "type":"surface_layer", "stage":"terrain", "block":"core:sandstone",
                      "priority":10, "surfaceField":"height", "surfaceOffset":-4,
                      "bottomField":"mask", "bottomOffset":2,
                      "maskField":"mask",
                      "maskCondition":{"op":"gt", "value":0.3},
                      "replaceTags":["terrain:rock"] },
                    { "id":"ore", "type":"volume", "stage":"addon", "block":"core:gravel",
                      "priority":20, "field":"ore",
                      "condition":{"op":"between", "value":0.7, "max":0.8},
                      "replaceTags":["terrain:rock"] }
                ],
                "anchorSets": [
                    { "id":"forest", "surfaceField":"height", "densityField":"mask",
                      "spacing":9, "chance":0.8, "salt":90,
                      "conditions":[{"field":"mask","op":"gt","value":0.2}] }
                ],
                "decorations": [
                    { "id":"grass", "type":"scatter", "anchorSet":"forest",
                      "block":"core:grass", "priority":200,
                      "replaceBlocks":["core:air"] },
                    { "id":"tree", "type":"structure", "anchorSet":"forest",
                      "priority":220, "script":"tree.lua", "function":"wood",
                      "palette":["core:stone"], "anchorMin":0.0, "anchorMax":0.7,
                      "bounds":{"minX":-2,"maxX":2,"minY":0,"maxY":8,"minZ":-2,"maxZ":2} }
                ]
            })" );
        const worldgen::WorldGenConfig cfg = worldgen::loadWorldGenConfig( path );
        removeTempConfig( path );
        CHECK_EQ( cfg.seed, 42u );
        CHECK_EQ( cfg.workerThreads, 6u );
        CHECK_EQ( cfg.surfaceField, std::string( "height" ) );
        CHECK_EQ( cfg.fields.size(), 3u );
        CHECK_EQ( cfg.passes.size(), 3u );
        CHECK_EQ( cfg.fields[2].functionName, std::string( "sampleOre" ) );
        CHECK_EQ( cfg.stages.size(), 2u );
        CHECK_EQ( cfg.stages[0].id, std::string( "terrain" ) );
        CHECK_EQ( cfg.stages[1].id, std::string( "addon" ) );
        CHECK_EQ( cfg.passes[0].stage, std::string( "terrain" ) );
        CHECK_EQ( cfg.passes[2].stage, std::string( "addon" ) );
        CHECK_EQ( cfg.passes[1].maskField, std::string( "mask" ) );
        CHECK_EQ( cfg.passes[1].surfaceOffset, -4 );
        CHECK_EQ( cfg.passes[1].bottomField, std::string( "mask" ) );
        CHECK_EQ( cfg.passes[1].bottomOffset, 2 );
        CHECK( cfg.passes[1].maskCondition.op == worldgen::CompareOp::Greater );
        CHECK_EQ( cfg.passes[2].replaceTags[0], std::string( "terrain:rock" ) );
        CHECK( cfg.passes[2].condition.op == worldgen::CompareOp::Between );
        CHECK_EQ( cfg.anchorSets.size(), 1u );
        CHECK_EQ( cfg.decorations.size(), 2u );
        CHECK_EQ( cfg.anchorSets[0].spacing, 9 );
        CHECK_EQ( cfg.decorations[1].palette[0], std::string( "core:stone" ) );
        CHECK_EQ( cfg.decorations[1].bounds.maxY, 8 );
    }

    TEST_CASE( worldgen_config_rejects_unknown_fields )
    {
        const auto path = writeTempConfig(
            "clonecraft-worldgen-bad.json",
            R"({ "seed":1, "definitelyNotASetting":true, "fields":[], "passes":[] })" );
        bool threw = false;
        try { (void)worldgen::loadWorldGenConfig( path ); }
        catch( const std::exception &e )
        {
            threw = std::string( e.what() ).find( "unknown field" ) != std::string::npos;
        }
        removeTempConfig( path );
        CHECK( threw );
    }

    TEST_CASE( worldgen_config_rejects_unknown_pass_field_reference )
    {
        const auto path = writeTempConfig(
            "clonecraft-worldgen-missing-field.json",
            R"({
                "surfaceField":"height",
                "fields":[{"id":"height","dimension":"2d","script":"height.lua"}],
                "passes":[{"id":"bad","type":"volume","stage":"terrain","block":"core:stone","field":"missing"}]
            })" );
        bool threw = false;
        try { (void)worldgen::loadWorldGenConfig( path ); }
        catch( const std::exception &e )
        {
            threw = std::string( e.what() ).find( "unknown field" ) != std::string::npos;
        }
        removeTempConfig( path );
        CHECK( threw );
    }
    TEST_CASE( worldgen_config_loads_mod_defined_stage_without_engine_changes )
    {
        const auto path = writeTempConfig(
            "clonecraft-worldgen-custom-stage.json",
            R"({
                "surfaceField":"height",
                "fields":[{"id":"height","dimension":"2d","script":"height.lua"}],
                "passes":[
                    {"id":"base","type":"fill_below","stage":"terrain",
                     "block":"core:stone","field":"height"},
                    {"id":"erosion","type":"surface","stage":"erosion",
                     "block":"core:air","field":"height"}
                ]
            })",
            R"({"stages":[
                {"id":"terrain","order":0},
                {"id":"erosion","order":50},
                {"id":"addon","order":100}
            ]})" );
        const worldgen::WorldGenConfig cfg = worldgen::loadWorldGenConfig( path );
        removeTempConfig( path );
        CHECK_EQ( cfg.stages.size(), 3u );
        CHECK_EQ( cfg.stages[1].id, std::string( "erosion" ) );
        CHECK_EQ( cfg.passes[1].stage, std::string( "erosion" ) );
    }

    TEST_CASE( worldgen_config_rejects_unknown_pass_stage )
    {
        const auto path = writeTempConfig(
            "clonecraft-worldgen-bad-stage.json",
            R"({
                "surfaceField":"height",
                "fields":[{"id":"height","dimension":"2d","script":"height.lua"}],
                "passes":[{"id":"bad","type":"fill_below","stage":"vegetation",
                           "block":"core:stone","field":"height"}]
            })" );
        bool threw = false;
        try { (void)worldgen::loadWorldGenConfig( path ); }
        catch( const std::exception &e )
        {
            threw = std::string( e.what() ).find( "stage" ) != std::string::npos;
        }
        removeTempConfig( path );
        CHECK( threw );
    }

}

int main() { return test::runAll(); }
