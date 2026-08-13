#include "config/Settings.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace config
{
    namespace
    {
        using json = nlohmann::json;

        template <typename T>
        T boundedNumber( const json &object, const char *field, T fallback,
                         T minValue, T maxValue, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_number() )
                throw std::runtime_error( std::string( where ) + "." + field + " must be a number" );
            const T value = object[field].get<T>();
            if( value < minValue || value > maxValue )
                throw std::runtime_error( std::string( where ) + "." + field + " is outside the allowed range" );
            return value;
        }

        bool optionalBool( const json &object, const char *field, bool fallback, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_boolean() )
                throw std::runtime_error( std::string( where ) + "." + field + " must be boolean" );
            return object[field].get<bool>();
        }

        std::string optionalString( const json &object, const char *field,
                                    const std::string &fallback, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_string() )
                throw std::runtime_error( std::string( where ) + "." + field + " must be a string" );
            const std::string value = object[field].get<std::string>();
            if( value.empty() )
                throw std::runtime_error( std::string( where ) + "." + field + " must not be empty" );
            return value;
        }


        bool isHexColour( const std::string &value )
        {
            if( value.size() != 7u && value.size() != 9u )
                return false;
            if( value.front() != '#' )
                return false;
            for( std::size_t i = 1; i < value.size(); ++i )
            {
                const char c = value[i];
                const bool digit = c >= '0' && c <= '9';
                const bool lower = c >= 'a' && c <= 'f';
                const bool upper = c >= 'A' && c <= 'F';
                if( !digit && !lower && !upper )
                    return false;
            }
            return true;
        }

        std::string optionalColour( const json &object, const char *field,
                                    const std::string &fallback, const char *where )
        {
            const std::string value = optionalString( object, field, fallback, where );
            if( !isHexColour( value ) )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must be #RRGGBB or #RRGGBBAA" );
            return value;
        }

        json toJson( const Settings &settings )
        {
            json options = json::object();
            for( const auto &[name, value] : settings.ogre.configOptions )
                options[name] = value;

            return json{
                { "schema_version", settings.schemaVersion },
                { "window", {
                    { "width", settings.window.width },
                    { "height", settings.window.height },
                    { "fullscreen", settings.window.fullscreen },
                    { "resizable", settings.window.resizable }
                } },
                { "world", {
                    { "chunk_render_distance", settings.world.chunkRenderDistance },
                    { "chunk_commits_per_update", settings.world.chunkCommitsPerUpdate }
                } },
                { "camera", {
                    { "move_speed", settings.camera.moveSpeed },
                    { "mouse_sensitivity", settings.camera.mouseSensitivity }
                } },
                { "debug_hud", {
                    { "color", settings.debugHud.color },
                    { "font_size_px", settings.debugHud.fontSizePx }
                } },
                { "ogre", {
                    { "render_system", settings.ogre.renderSystem },
                    { "render_system_plugin", settings.ogre.renderSystemPlugin },
                    { "log_file", settings.ogre.logFile },
                    { "config_options", std::move( options ) },
                    { "shadow_far_distance", settings.ogre.shadowFarDistance },
                    { "camera_near_clip", settings.ogre.cameraNearClip },
                    { "camera_far_clip", settings.ogre.cameraFarClip },
                    { "forward3d", {
                        { "enabled", settings.ogre.forward3d.enabled },
                        { "width", settings.ogre.forward3d.width },
                        { "height", settings.ogre.forward3d.height },
                        { "depth", settings.ogre.forward3d.depth },
                        { "lights_per_cell", settings.ogre.forward3d.lightsPerCell },
                        { "min_distance", settings.ogre.forward3d.minDistance },
                        { "max_distance", settings.ogre.forward3d.maxDistance }
                    } }
                } }
            };
        }

        Settings parseSettings( const json &root, const std::filesystem::path &path )
        {
            if( !root.is_object() )
                throw std::runtime_error( path.string() + ": root must be a JSON object" );

            Settings out;
            if( root.contains( "schema_version" ) )
            {
                if( !root["schema_version"].is_number_unsigned() )
                    throw std::runtime_error( path.string() + ": schema_version must be an unsigned integer" );
                out.schemaVersion = root["schema_version"].get<std::uint32_t>();
                if( out.schemaVersion != 1u )
                    throw std::runtime_error( path.string() + ": unsupported settings schema_version " +
                                              std::to_string( out.schemaVersion ) );
            }

            if( root.contains( "window" ) )
            {
                const json &window = root["window"];
                if( !window.is_object() ) throw std::runtime_error( "settings.window must be an object" );
                out.window.width = boundedNumber<int>( window, "width", out.window.width, 320, 16384, "settings.window" );
                out.window.height = boundedNumber<int>( window, "height", out.window.height, 200, 8640, "settings.window" );
                out.window.fullscreen = optionalBool( window, "fullscreen", out.window.fullscreen, "settings.window" );
                out.window.resizable = optionalBool( window, "resizable", out.window.resizable, "settings.window" );
            }

            if( root.contains( "world" ) )
            {
                const json &world = root["world"];
                if( !world.is_object() ) throw std::runtime_error( "settings.world must be an object" );
                out.world.chunkRenderDistance = boundedNumber<std::int64_t>(
                    world, "chunk_render_distance", out.world.chunkRenderDistance, 0, 32, "settings.world" );
                out.world.chunkCommitsPerUpdate = boundedNumber<std::size_t>(
                    world, "chunk_commits_per_update", out.world.chunkCommitsPerUpdate,
                    std::size_t{ 1u }, std::size_t{ 1024u }, "settings.world" );
            }

            if( root.contains( "camera" ) )
            {
                const json &camera = root["camera"];
                if( !camera.is_object() ) throw std::runtime_error( "settings.camera must be an object" );
                out.camera.moveSpeed = boundedNumber<double>( camera, "move_speed", out.camera.moveSpeed,
                                                               0.01, 1000000.0, "settings.camera" );
                out.camera.mouseSensitivity = boundedNumber<double>(
                    camera, "mouse_sensitivity", out.camera.mouseSensitivity, 0.00001, 1.0, "settings.camera" );
            }

            if( root.contains( "debug_hud" ) )
            {
                const json &hud = root["debug_hud"];
                if( !hud.is_object() )
                    throw std::runtime_error( "settings.debug_hud must be an object" );
                out.debugHud.color = optionalColour( hud, "color", out.debugHud.color,
                                                     "settings.debug_hud" );
                out.debugHud.fontSizePx = boundedNumber<float>(
                    hud, "font_size_px", out.debugHud.fontSizePx, 8.0f, 96.0f,
                    "settings.debug_hud" );
            }

            if( root.contains( "ogre" ) )
            {
                const json &ogre = root["ogre"];
                if( !ogre.is_object() ) throw std::runtime_error( "settings.ogre must be an object" );
                out.ogre.renderSystem = optionalString( ogre, "render_system", out.ogre.renderSystem, "settings.ogre" );
                out.ogre.renderSystemPlugin = optionalString(
                    ogre, "render_system_plugin", out.ogre.renderSystemPlugin, "settings.ogre" );
                out.ogre.logFile = optionalString( ogre, "log_file", out.ogre.logFile, "settings.ogre" );
                out.ogre.shadowFarDistance = boundedNumber<float>(
                    ogre, "shadow_far_distance", out.ogre.shadowFarDistance, 0.0f, 1000000.0f, "settings.ogre" );
                out.ogre.cameraNearClip = boundedNumber<float>(
                    ogre, "camera_near_clip", out.ogre.cameraNearClip, 0.001f, 1000.0f, "settings.ogre" );
                out.ogre.cameraFarClip = boundedNumber<float>(
                    ogre, "camera_far_clip", out.ogre.cameraFarClip, 1.0f, 10000000.0f, "settings.ogre" );
                if( out.ogre.cameraFarClip <= out.ogre.cameraNearClip )
                    throw std::runtime_error( "settings.ogre.camera_far_clip must be greater than camera_near_clip" );

                if( ogre.contains( "config_options" ) )
                {
                    const json &options = ogre["config_options"];
                    if( !options.is_object() )
                        throw std::runtime_error( "settings.ogre.config_options must be an object of string values" );
                    out.ogre.configOptions.clear();
                    for( auto it = options.begin(); it != options.end(); ++it )
                    {
                        if( !it.value().is_string() )
                            throw std::runtime_error( "settings.ogre.config_options.'" + it.key() + "' must be a string" );
                        out.ogre.configOptions[it.key()] = it.value().get<std::string>();
                    }
                }

                if( ogre.contains( "forward3d" ) )
                {
                    const json &forward = ogre["forward3d"];
                    if( !forward.is_object() ) throw std::runtime_error( "settings.ogre.forward3d must be an object" );
                    out.ogre.forward3d.enabled = optionalBool( forward, "enabled", out.ogre.forward3d.enabled, "settings.ogre.forward3d" );
                    out.ogre.forward3d.width = boundedNumber<std::uint32_t>( forward, "width", out.ogre.forward3d.width, 1u, 64u, "settings.ogre.forward3d" );
                    out.ogre.forward3d.height = boundedNumber<std::uint32_t>( forward, "height", out.ogre.forward3d.height, 1u, 64u, "settings.ogre.forward3d" );
                    out.ogre.forward3d.depth = boundedNumber<std::uint32_t>( forward, "depth", out.ogre.forward3d.depth, 1u, 64u, "settings.ogre.forward3d" );
                    out.ogre.forward3d.lightsPerCell = boundedNumber<std::uint32_t>( forward, "lights_per_cell", out.ogre.forward3d.lightsPerCell, 1u, 4096u, "settings.ogre.forward3d" );
                    out.ogre.forward3d.minDistance = boundedNumber<float>( forward, "min_distance", out.ogre.forward3d.minDistance, 0.001f, 1000000.0f, "settings.ogre.forward3d" );
                    out.ogre.forward3d.maxDistance = boundedNumber<float>( forward, "max_distance", out.ogre.forward3d.maxDistance, 0.01f, 1000000.0f, "settings.ogre.forward3d" );
                    if( out.ogre.forward3d.maxDistance <= out.ogre.forward3d.minDistance )
                        throw std::runtime_error( "settings.ogre.forward3d.max_distance must be greater than min_distance" );
                }
            }

            return out;
        }

        json readJson( const std::filesystem::path &path )
        {
            std::ifstream file( path, std::ios::binary );
            if( !file )
                throw std::runtime_error( "cannot open settings file '" + path.string() + "'" );
            try
            {
                return json::parse( file );
            }
            catch( const json::parse_error &error )
            {
                throw std::runtime_error( path.string() + ": JSON parse error at byte " +
                                          std::to_string( error.byte ) + ": " + error.what() );
            }
        }
    } // namespace

    std::filesystem::path defaultSettingsPath()
    {
        if( const char *home = std::getenv( "HOME" ); home && *home )
            return std::filesystem::path( home ) / ".config" / "Omnigrid" / "settings.json";
        throw std::runtime_error( "cannot determine Omnigrid settings path: HOME is not set" );
    }

    void saveSettings( const std::filesystem::path &path, const Settings &settings )
    {
        std::error_code error;
        const std::filesystem::path parent = path.parent_path();
        if( !parent.empty() )
        {
            std::filesystem::create_directories( parent, error );
            if( error )
                throw std::runtime_error( "cannot create settings directory '" + parent.string() + "': " + error.message() );
        }

        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream file( temporary, std::ios::binary | std::ios::trunc );
            if( !file )
                throw std::runtime_error( "cannot write settings file '" + temporary.string() + "'" );
            file << toJson( settings ).dump( 2 ) << '\n';
            if( !file )
                throw std::runtime_error( "failed while writing settings file '" + temporary.string() + "'" );
        }

        std::filesystem::rename( temporary, path, error );
        if( error )
        {
            // POSIX rename replaces atomically, but keep a fallback for filesystems/platforms
            // that reject replacement of an existing destination.
            std::error_code ignored;
            std::filesystem::remove( path, ignored );
            error.clear();
            std::filesystem::rename( temporary, path, error );
        }
        if( error )
            throw std::runtime_error( "cannot install settings file '" + path.string() + "': " + error.message() );
    }

    Settings loadOrCreateSettings( const std::filesystem::path &path )
    {
        std::error_code error;
        const bool exists = std::filesystem::exists( path, error );
        if( error )
            throw std::runtime_error( "cannot inspect settings file '" + path.string() + "': " + error.message() );

        if( !exists )
        {
            Settings defaults;
            saveSettings( path, defaults );
            return defaults;
        }

        return parseSettings( readJson( path ), path );
    }

    Settings loadOrCreateSettings()
    {
        return loadOrCreateSettings( defaultSettingsPath() );
    }
} // namespace config
