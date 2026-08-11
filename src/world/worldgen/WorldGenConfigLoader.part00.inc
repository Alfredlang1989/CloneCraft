#include "world/worldgen/WorldGenConfigLoader.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace worldgen
{
    namespace
    {
        using json = nlohmann::json;

        [[noreturn]] void configError( const std::filesystem::path &path,
                                       const std::string &message )
        {
            throw std::runtime_error( path.string() + ": " + message );
        }

        template <std::size_t N>
        void checkUnknownFields( const json &object, const std::array<std::string_view, N> &allowed,
                                 const std::filesystem::path &path, const std::string &scope )
        {
            if( !object.is_object() )
                configError( path, scope + " must be an object" );
            for( const auto &[key, value] : object.items() )
            {
                (void)value;
                bool known = false;
                for( const std::string_view candidate : allowed )
                    known = known || key == candidate;
                if( !known )
                {
                    std::string message = "unknown field '";
                    message.append( scope ).push_back( '.' );
                    message.append( key ).push_back( '\'' );
                    configError( path, message );
                }
            }
        }

        template <typename T>
        void readOptional( const json &root, const char *name, T &target,
                           const std::filesystem::path &path, const std::string &scope )
        {
            const auto it = root.find( name );
            if( it == root.end() )
                return;
            try
            {
                target = ( *it ).get<T>();
            }
            catch( const json::exception & )
            {
                configError( path, "field '" + scope + "." + name + "' has the wrong type" );
            }
        }

        const json *optionalObject( const json &root, const char *name,
                                    const std::filesystem::path &path,
                                    const std::string &scope = "worldgen" )
        {
            const auto it = root.find( name );
            if( it == root.end() )
                return nullptr;
            if( !it->is_object() )
                configError( path, "field '" + scope + "." + name + "' must be an object" );
            return &( *it );
        }

        std::string requireString( const json &object, const char *name,
                                   const std::filesystem::path &path,
                                   const std::string &scope )
        {
            const auto it = object.find( name );
            if( it == object.end() || !it->is_string() )
                configError( path, "field '" + scope + "." + name + "' must be a string" );
            const std::string value = it->get<std::string>();
            if( value.empty() )
                configError( path, "field '" + scope + "." + name + "' must not be empty" );
            return value;
        }

        std::vector<std::string> readStringArray( const json &object, const char *name,
                                                   const std::filesystem::path &path,
                                                   const std::string &scope )
        {
            std::vector<std::string> result;
            const auto it = object.find( name );
            if( it == object.end() )
                return result;
            if( !it->is_array() )
                configError( path, "field '" + scope + "." + name +
                                   "' must be an array of strings" );
            std::unordered_set<std::string> seen;
            for( const json &value : *it )
            {
                if( !value.is_string() )
                    configError( path, "field '" + scope + "." + name +
                                       "' must contain only strings" );
                std::string text = value.get<std::string>();
                if( text.empty() )
                    configError( path, "field '" + scope + "." + name +
                                       "' must not contain empty strings" );
                if( !seen.insert( text ).second )
                {
                    std::string message = "field '";
                    message.append( scope ).push_back( '.' );
                    message.append( name ).append( "' contains duplicate '" );
                    message.append( text ).push_back( '\'' );
                    configError( path, message );
                }
                result.push_back( std::move( text ) );
            }
            return result;
        }

        FieldDimension parseDimension( const std::string &value,
                                       const std::filesystem::path &path,
                                       const std::string &scope )
        {
            if( value == "2d" ) return FieldDimension::D2;
            if( value == "3d" ) return FieldDimension::D3;
            configError( path, "field '" + scope + ".dimension' must be '2d' or '3d'" );
        }

        PassType parsePassType( const std::string &value, const std::filesystem::path &path,
                                const std::string &scope )
        {
            if( value == "fill_below" ) return PassType::FillBelow;
            if( value == "surface_layer" ) return PassType::SurfaceLayer;
            if( value == "surface" ) return PassType::Surface;
            if( value == "volume" ) return PassType::Volume;
            configError( path, "field '" + scope + ".type' has unknown pass type '" + value + "'" );
        }

        std::vector<StageConfig> loadStageRegistry( const std::filesystem::path &path )
        {
            std::ifstream input( path );
            if( !input )
                configError( path, "could not open stage registry" );

            json root;
            try
            {
                input >> root;
            }
            catch( const json::parse_error &e )
            {
                configError( path, "JSON parse error at byte " + std::to_string( e.byte ) );
            }
            constexpr std::array rootAllowed{ std::string_view( "stages" ) };
            checkUnknownFields( root, rootAllowed, path, "stageRegistry" );

