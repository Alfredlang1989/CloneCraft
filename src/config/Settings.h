#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace config
{
    struct WindowSettings
    {
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool resizable = true;
    };

    struct WorldSettings
    {
        std::int64_t chunkRenderDistance = 3;
        std::size_t chunkCommitsPerUpdate = 4u;
    };

    struct CameraSettings
    {
        double moveSpeed = 40.0;
        double mouseSensitivity = 0.0025;
    };

    struct Forward3DSettings
    {
        bool enabled = true;
        std::uint32_t width = 4u;
        std::uint32_t height = 4u;
        std::uint32_t depth = 4u;
        std::uint32_t lightsPerCell = 32u;
        float minDistance = 1.0f;
        float maxDistance = 120.0f;
    };

    struct DebugHudSettings
    {
        std::string color = "#D070FF";
        float fontSizePx = 18.0f;
    };

    struct OgreSettings
    {
        std::string renderSystem = "OpenGL 3+ Rendering Subsystem";
        std::string renderSystemPlugin = "RenderSystem_GL3Plus";
        std::string logFile = "clonecraft.log";
        std::map<std::string, std::string> configOptions{
            { "sRGB Gamma Conversion", "Yes" }
        };
        float shadowFarDistance = 220.0f;
        float cameraNearClip = 0.1f;
        float cameraFarClip = 1000.0f;
        Forward3DSettings forward3d;
    };

    struct Settings
    {
        std::uint32_t schemaVersion = 1u;
        WindowSettings window;
        WorldSettings world;
        CameraSettings camera;
        DebugHudSettings debugHud;
        OgreSettings ogre;
    };

    /** Linux path: $HOME/.config/Clonecraft/settings.json. */
    std::filesystem::path defaultSettingsPath();

    /** Loads settings from path, creating parent directories + a default file when absent. */
    Settings loadOrCreateSettings( const std::filesystem::path &path );

    /** Convenience overload using defaultSettingsPath(). */
    Settings loadOrCreateSettings();

    /** Atomically writes a complete settings file. Intended for a future in-game settings UI too. */
    void saveSettings( const std::filesystem::path &path, const Settings &settings );
} // namespace config
