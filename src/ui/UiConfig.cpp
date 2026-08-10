#include "ui/UiConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ui
{
    namespace
    {
        using json = nlohmann::json;

        std::string readText( const std::filesystem::path &path )
        {
            std::ifstream file( path, std::ios::binary );
            if( !file )
                throw std::runtime_error( "cannot open UI config '" + path.string() + "'" );
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        void rejectUnknown( const json &object, const char *where,
                            std::initializer_list<const char *> allowed )
        {
            if( !object.is_object() )
                throw std::runtime_error( std::string( where ) + " must be an object" );
            for( auto it = object.begin(); it != object.end(); ++it )
            {
                bool known = false;
                for( const char *name : allowed )
                    known = known || it.key() == name;
                if( !known )
                    throw std::runtime_error( std::string( where ) + ": unknown field '" +
                                              it.key() + "'" );
            }
        }

        float boundedFloat( const json &object, const char *field, float fallback,
                            float minValue, float maxValue, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_number() )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must be a number" );
            const float value = object[field].get<float>();
            if( value < minValue || value > maxValue )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " is outside the allowed range" );
            return value;
        }

        double boundedDouble( const json &object, const char *field, double fallback,
                              double minValue, double maxValue, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_number() )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must be a number" );
            const double value = object[field].get<double>();
            if( value < minValue || value > maxValue )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " is outside the allowed range" );
            return value;
        }

        bool optionalBool( const json &object, const char *field, bool fallback,
                           const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_boolean() )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must be boolean" );
            return object[field].get<bool>();
        }

        std::string optionalString( const json &object, const char *field,
                                    const std::string &fallback, const char *where )
        {
            if( !object.contains( field ) )
                return fallback;
            if( !object[field].is_string() )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must be a string" );
            const std::string value = object[field].get<std::string>();
            if( value.empty() )
                throw std::runtime_error( std::string( where ) + "." + field +
                                          " must not be empty" );
            return value;
        }

        int hexNibble( char c )
        {
            if( c >= '0' && c <= '9' ) return c - '0';
            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            if( c >= 'a' && c <= 'f' ) return 10 + c - 'a';
            return -1;
        }

        float parseHexByte( const std::string &text, std::size_t offset )
        {
            const int hi = hexNibble( text[offset] );
            const int lo = hexNibble( text[offset + 1] );
            if( hi < 0 || lo < 0 )
                throw std::runtime_error( "ui.blockSelection.color contains a non-hex digit" );
            return static_cast<float>( ( hi << 4 ) | lo ) / 255.0f;
        }

        UiColor parseColor( const json &value )
        {
            if( value.is_string() )
            {
                const std::string text = value.get<std::string>();
                if( ( text.size() != 7u && text.size() != 9u ) || text[0] != '#' )
                    throw std::runtime_error(
                        "ui.blockSelection.color must be #RRGGBB or #RRGGBBAA" );
                UiColor out;
                out.r = parseHexByte( text, 1 );
                out.g = parseHexByte( text, 3 );
                out.b = parseHexByte( text, 5 );
                out.a = text.size() == 9u ? parseHexByte( text, 7 ) : 1.0f;
                return out;
            }

            if( value.is_array() && ( value.size() == 3u || value.size() == 4u ) )
            {
                UiColor out;
                float *channels[4] = { &out.r, &out.g, &out.b, &out.a };
                for( std::size_t i = 0; i < value.size(); ++i )
                {
                    if( !value[i].is_number() )
                        throw std::runtime_error(
                            "ui.blockSelection.color array must contain numbers" );
                    const float channel = value[i].get<float>();
                    if( channel < 0.0f || channel > 1.0f )
                        throw std::runtime_error(
                            "ui.blockSelection.color channels must be in 0..1" );
                    *channels[i] = channel;
                }
                return out;
            }

            throw std::runtime_error(
                "ui.blockSelection.color must be #RRGGBB/#RRGGBBAA or [r,g,b,a]" );
        }
    } // namespace

    UiConfig loadUiConfig( const std::filesystem::path &path )
    {
        json root;
        try
        {
            root = json::parse( readText( path ) );
        }
        catch( const json::parse_error &error )
        {
            throw std::runtime_error( path.string() + ": JSON parse error at byte " +
                                      std::to_string( error.byte ) + ": " + error.what() );
        }

        rejectUnknown( root, "ui", { "crosshair", "blockSelection" } );
        UiConfig result;

        if( root.contains( "crosshair" ) )
        {
            const json &crosshair = root["crosshair"];
            rejectUnknown( crosshair, "ui.crosshair",
                           { "enabled", "texture", "sizePixels", "opacity" } );
            result.crosshair.enabled = optionalBool(
                crosshair, "enabled", result.crosshair.enabled, "ui.crosshair" );
            result.crosshair.texture = optionalString(
                crosshair, "texture", result.crosshair.texture, "ui.crosshair" );
            result.crosshair.sizePixels = boundedFloat(
                crosshair, "sizePixels", result.crosshair.sizePixels, 4.0f, 256.0f,
                "ui.crosshair" );
            result.crosshair.opacity = boundedFloat(
                crosshair, "opacity", result.crosshair.opacity, 0.0f, 1.0f,
                "ui.crosshair" );
        }

        if( root.contains( "blockSelection" ) )
        {
            const json &selection = root["blockSelection"];
            rejectUnknown( selection, "ui.blockSelection",
                           { "enabled", "maxDistance", "color", "thickness", "expand",
                             "depthTest" } );
            result.blockSelection.enabled = optionalBool(
                selection, "enabled", result.blockSelection.enabled, "ui.blockSelection" );
            result.blockSelection.maxDistance = boundedDouble(
                selection, "maxDistance", result.blockSelection.maxDistance, 0.25, 128.0,
                "ui.blockSelection" );
            if( selection.contains( "color" ) )
                result.blockSelection.color = parseColor( selection["color"] );
            result.blockSelection.thickness = boundedFloat(
                selection, "thickness", result.blockSelection.thickness, 0.001f, 0.25f,
                "ui.blockSelection" );
            result.blockSelection.expand = boundedFloat(
                selection, "expand", result.blockSelection.expand, 0.0f, 0.10f,
                "ui.blockSelection" );
            result.blockSelection.depthTest = optionalBool(
                selection, "depthTest", result.blockSelection.depthTest,
                "ui.blockSelection" );
        }

        return result;
    }
} // namespace ui
