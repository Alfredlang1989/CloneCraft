#include "TestHarness.h"
#include "ui/UiConfig.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE(ui_config_loads_data_driven_crosshair_and_selection)
{
    const ui::UiConfig config = ui::loadUiConfig(
        std::filesystem::path( OMNIGRID_DATA_DIR ) / "ui.json" );
    CHECK( config.crosshair.enabled );
    CHECK( config.crosshair.texture == "ui/crosshair.png" );
    CHECK( config.crosshair.sizePixels == 20.0f );
    CHECK( config.blockSelection.enabled );
    CHECK( config.blockSelection.maxDistance == 8.0 );
    CHECK( config.blockSelection.thickness == 0.028f );
    CHECK( config.blockSelection.color.a > 0.79f && config.blockSelection.color.a < 0.81f );
}

TEST_CASE(ui_config_rejects_unknown_fields)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "omnigrid-ui-invalid.json";
    {
        std::ofstream out( path );
        out << R"({"crosshair":{"telepathy":true}})";
    }

    bool rejected = false;
    try
    {
        (void)ui::loadUiConfig( path );
    }
    catch( const std::runtime_error & )
    {
        rejected = true;
    }
    std::error_code ignored;
    std::filesystem::remove( path, ignored );
    CHECK( rejected );
}

int main() { return test::runAll(); }
