#include "WorldGenConfigLoader.part00.inc"

#define loadWorldGenConfig loadBaseWorldGenConfig
#include "WorldGenConfigLoader.part01.inc"
#include "WorldGenConfigLoader.part02.inc"
#include "WorldGenConfigLoader.part03.inc"
#include "WorldGenConfigLoader.part04.inc"
#undef loadWorldGenConfig

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace worldgen
{
    WorldGenConfig loadWorldGenConfig( const std::filesystem::path &path )
    {
        WorldGenConfig cfg = loadBaseWorldGenConfig( path );

        const std::filesystem::path registryPath = path.parent_path() / "worldgen_fields.json";
        if( !std::filesystem::exists( registryPath ) ) return cfg;

        std::ifstream input( registryPath );
        if( !input )
            throw std::runtime_error( registryPath.string() + ": could not open field registry" );

        nlohmann::json root;
        try
        {
            input >> root;
        }
        catch( const nlohmann::json::parse_error &e )
        {
            throw std::runtime_error( registryPath.string() + ": JSON parse error at byte " +
                                      std::to_string( e.byte ) );
        }
        if( !root.is_object() || root.size() != 1u || !root.contains( "fields" ) ||
            !root["fields"].is_array() )
            throw std::runtime_error( registryPath.string() +
                                      ": root must contain only a 'fields' array" );

        std::unordered_set<std::string> fieldIds;
        for( const FieldConfig &field : cfg.fields ) fieldIds.insert( field.id );

        std::size_t index = 0;
        for( const nlohmann::json &entry : root["fields"] )
        {
            const std::string scope = "fields[" + std::to_string( index++ ) + "]";
            if( !entry.is_object() )
                throw std::runtime_error( registryPath.string() + ": " + scope +
                                          " must be an object" );
            for( const auto &[key, value] : entry.items() )
            {
                (void)value;
                if( key != "id" && key != "dimension" && key != "script" &&
                    key != "function" && key != "salt" )
                    throw std::runtime_error( registryPath.string() + ": unknown field '" +
                                              scope + "." + key + "'" );
            }

            auto requireString = [&]( const char *name ) {
                const auto it = entry.find( name );
                if( it == entry.end() || !it->is_string() || it->get<std::string>().empty() )
                    throw std::runtime_error( registryPath.string() + ": field '" + scope +
                                              "." + name + "' must be a non-empty string" );
                return it->get<std::string>();
            };

            FieldConfig field;
            field.id = requireString( "id" );
            if( !fieldIds.insert( field.id ).second )
                throw std::runtime_error( registryPath.string() + ": duplicate field id '" +
                                          field.id + "'" );

            const std::string dimension = requireString( "dimension" );
            if( dimension == "2d" ) field.dimension = FieldDimension::D2;
            else if( dimension == "3d" ) field.dimension = FieldDimension::D3;
            else
                throw std::runtime_error( registryPath.string() + ": field '" + scope +
                                          ".dimension' must be '2d' or '3d'" );

            field.scriptPath = registryPath.parent_path() / requireString( "script" );
            if( const auto it = entry.find( "function" ); it != entry.end() )
            {
                if( !it->is_string() || it->get<std::string>().empty() )
                    throw std::runtime_error( registryPath.string() + ": field '" + scope +
                                              ".function' must be a non-empty string" );
                field.functionName = it->get<std::string>();
            }
            if( const auto it = entry.find( "salt" ); it != entry.end() )
            {
                if( !it->is_number_unsigned() && !it->is_number_integer() )
                    throw std::runtime_error( registryPath.string() + ": field '" + scope +
                                              ".salt' must be an integer" );
                try { field.salt = it->get<std::uint64_t>(); }
                catch( const nlohmann::json::exception & )
                {
                    throw std::runtime_error( registryPath.string() + ": field '" + scope +
                                              ".salt' is outside uint64 range" );
                }
            }
            cfg.fields.push_back( std::move( field ) );
        }

        return cfg;
    }
} // namespace worldgen
