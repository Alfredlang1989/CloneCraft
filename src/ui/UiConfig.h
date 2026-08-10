#pragma once

#include <filesystem>
#include <string>

namespace ui
{
    struct UiColor
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        friend bool operator==( const UiColor &, const UiColor & ) = default;
    };

    struct CrosshairConfig
    {
        bool enabled = true;
        std::string texture = "ui/crosshair.png";
        float sizePixels = 20.0f;
        float opacity = 1.0f;
    };

    struct BlockSelectionConfig
    {
        bool enabled = true;
        double maxDistance = 8.0;
        UiColor color{ 1.0f, 1.0f, 1.0f, 0.78f };
        float thickness = 0.028f;
        float expand = 0.004f;
        bool depthTest = false;
    };

    struct UiConfig
    {
        CrosshairConfig crosshair;
        BlockSelectionConfig blockSelection;
    };

    /** Loads and validates the data-driven first-person UI configuration. */
    UiConfig loadUiConfig( const std::filesystem::path &path );
} // namespace ui
