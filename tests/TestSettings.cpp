#include "TestHarness.h"
#include "config/Settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path freshPath( const char *name )
    {
        const auto root = std::filesystem::temp_directory_path() / "clonecraft-settings-tests";
        std::error_code ignored;
        std::filesystem::remove_all( root, ignored );
        return root / name / "settings.json";
    }
}


TEST_CASE(settings_default_linux_path_uses_home_dot_config)
{
    const char *oldHome = std::getenv( "HOME" );
    const std::string savedHome = oldHome ? oldHome : "";
    const bool hadHome = oldHome != nullptr;
    setenv( "HOME", "/tmp/clonecraft-home-probe", 1 );
    const auto path = config::defaultSettingsPath();
    if( hadHome ) setenv( "HOME", savedHome.c_str(), 1 );
    else unsetenv( "HOME" );
    CHECK( path == std::filesystem::path( "/tmp/clonecraft-home-probe/.config/Clonecraft/settings.json" ) );
}

TEST_CASE(settings_missing_file_is_created_with_defaults)
{
    const auto path = freshPath( "create-default" );
    const config::Settings settings = config::loadOrCreateSettings( path );
    CHECK( std::filesystem::exists( path ) );
    CHECK_EQ( settings.window.width, 1280 );
    CHECK_EQ( settings.window.height, 720 );
    CHECK_EQ( settings.world.chunkRenderDistance, 3 );
    CHECK( settings.ogre.configOptions.at( "sRGB Gamma Conversion" ) == "Yes" );
}

TEST_CASE(settings_custom_values_are_loaded)
{
    const auto path = freshPath( "custom" );
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream out( path );
    out << R"({
      "schema_version": 1,
      "window": {"width": 1920, "height": 1080, "fullscreen": true, "resizable": false},
      "world": {"chunk_render_distance": 7, "chunk_commits_per_update": 12},
      "camera": {"move_speed": 77.5, "mouse_sensitivity": 0.004},
      "ogre": {
        "render_system": "OpenGL 3+ Rendering Subsystem",
        "render_system_plugin": "RenderSystem_GL3Plus",
        "log_file": "ogre-custom.log",
        "config_options": {"sRGB Gamma Conversion": "Yes", "VSync": "No"},
        "shadow_far_distance": 333.0,
        "camera_near_clip": 0.2,
        "camera_far_clip": 2500.0,
        "forward3d": {"enabled": true, "width": 8, "height": 5, "depth": 6,
                      "lights_per_cell": 64, "min_distance": 0.5, "max_distance": 180.0}
      }
    })";
    out.close();

    const config::Settings settings = config::loadOrCreateSettings( path );
    CHECK_EQ( settings.window.width, 1920 );
    CHECK( settings.window.fullscreen );
    CHECK_EQ( settings.world.chunkRenderDistance, 7 );
    CHECK_EQ( settings.world.chunkCommitsPerUpdate, std::size_t{ 12 } );
    CHECK( settings.camera.moveSpeed == 77.5 );
    CHECK( settings.ogre.configOptions.at( "VSync" ) == "No" );
    CHECK( settings.ogre.forward3d.width == 8u );
    CHECK( settings.ogre.cameraFarClip == 2500.0f );
}

TEST_CASE(settings_reject_invalid_render_distance)
{
    const auto path = freshPath( "invalid" );
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream out( path );
    out << R"({"world":{"chunk_render_distance":999999}})";
    out.close();

    bool rejected = false;
    try
    {
        (void)config::loadOrCreateSettings( path );
    }
    catch( const std::runtime_error & )
    {
        rejected = true;
    }
    CHECK( rejected );
}

TEST_CASE(settings_save_roundtrip)
{
    const auto path = freshPath( "roundtrip" );
    config::Settings original;
    original.window.width = 2560;
    original.window.height = 1440;
    original.world.chunkRenderDistance = 5;
    original.camera.mouseSensitivity = 0.00325;
    original.ogre.configOptions["Some Future Ogre Option"] = "Fancy";
    config::saveSettings( path, original );

    const config::Settings loaded = config::loadOrCreateSettings( path );
    CHECK_EQ( loaded.window.width, 2560 );
    CHECK_EQ( loaded.window.height, 1440 );
    CHECK_EQ( loaded.world.chunkRenderDistance, 5 );
    CHECK( loaded.camera.mouseSensitivity == 0.00325 );
    CHECK( loaded.ogre.configOptions.at( "Some Future Ogre Option" ) == "Fancy" );
}

int main() { return test::runAll(); }
