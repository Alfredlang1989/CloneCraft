#include "world/registry/RegistryLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace world
{
    namespace
    {
        using json = nlohmann::json;

        std::string context( const std::string &source, int index )
        {
            return source + ": entry " + std::to_string( index );
        }

        std::string readTextFile( const std::filesystem::path &path, const std::string &label )
        {
            std::ifstream file( path, std::ios::binary );
            if( !file )
                throw RegistryError( "cannot open " + label + " (" + path.string() + ")" );
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        json parseJson( const std::string &text, const std::string &source )
        {
            try
            {
                return json::parse( text );
            }
            catch( const json::parse_error &e )
            {
                throw RegistryError( source + ": JSON parse error at byte " +
                                     std::to_string( e.byte ) + ": " + e.what() );
            }
        }

        int hexNibble( char c )
        {
            if( c >= '0' && c <= '9' ) return c - '0';
            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            if( c >= 'a' && c <= 'f' ) return 10 + ( c - 'a' );
            return -1;
        }

        std::uint8_t hexByte( const std::string &value, std::size_t offset,
                              const std::string &where )
        {
            const int hi = hexNibble( value[offset] );
            const int lo = hexNibble( value[offset + 1] );
            if( hi < 0 || lo < 0 )
                throw RegistryError( where + ": 'color' contains a non-hex digit" );
            return static_cast<std::uint8_t>( ( hi << 4 ) | lo );
        }

        Rgba8 parseColor( const json &value, const std::string &source, int index )
        {
            const std::string where = context( source, index );

            if( value.is_string() )
            {
                const std::string text = value.get<std::string>();
                if( text.size() != 7u && text.size() != 9u )
                    throw RegistryError( where +
                                         ": 'color' string must be #RRGGBB or #RRGGBBAA" );
                if( text[0] != '#' )
                    throw RegistryError( where + ": 'color' string must start with '#'" );

                Rgba8 result;
                result.r = hexByte( text, 1, where );
                result.g = hexByte( text, 3, where );
                result.b = hexByte( text, 5, where );
                result.a = text.size() == 9u ? hexByte( text, 7, where ) : 255u;
                return result;
            }

            if( value.is_array() )
            {
                if( value.size() != 3u && value.size() != 4u )
                    throw RegistryError( where +
                                         ": 'color' array must contain [r,g,b] or [r,g,b,a]" );

                Rgba8 result;
                std::uint8_t *channels[4] = { &result.r, &result.g, &result.b, &result.a };
                for( std::size_t i = 0; i < value.size(); ++i )
                {
                    if( !value[i].is_number_integer() )
                        throw RegistryError( where + ": 'color' channels must be integers" );
                    const int channel = value[i].get<int>();
                    if( channel < 0 || channel > 255 )
                        throw RegistryError( where + ": 'color' channels must be in 0..255" );
                    *channels[i] = static_cast<std::uint8_t>( channel );
                }
                return result;
            }

            throw RegistryError( where +
                                 ": 'color' must be #RRGGBB/#RRGGBBAA or an RGB(A) array" );
        }

        float optionalFloatInRange( const json &entry, const std::string &source, int index,
                                    const char *field, float fallback, float minValue,
                                    float maxValue )
        {
            if( !entry.contains( field ) )
                return fallback;
            if( !entry[field].is_number() )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must be a number" );
            const float value = entry[field].get<float>();
            if( value < minValue || value > maxValue )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must be in " + std::to_string( minValue ) + ".." +
                                     std::to_string( maxValue ) );
            return value;
        }

        bool optionalBool( const json &entry, const std::string &source, int index,
                           const char *field, bool fallback )
        {
            if( !entry.contains( field ) )
                return fallback;
            if( !entry[field].is_boolean() )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must be a boolean" );
            return entry[field].get<bool>();
        }

        std::string optionalNonEmptyString( const json &entry, const std::string &source, int index,
                                            const char *field )
        {
            if( !entry.contains( field ) )
                return {};
            if( !entry[field].is_string() )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must be a string" );
            const std::string value = entry[field].get<std::string>();
            if( value.empty() )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must not be empty" );
            return value;
        }

        std::vector<std::string> optionalStringArray( const json &entry,
                                                       const std::string &source, int index,
                                                       const char *field )
        {
            std::vector<std::string> result;
            if( !entry.contains( field ) )
                return result;
            if( !entry[field].is_array() )
                throw RegistryError( context( source, index ) + ": '" + field +
                                     "' must be an array of strings" );

            for( const json &value : entry[field] )
            {
                if( !value.is_string() )
                    throw RegistryError( context( source, index ) + ": '" + field +
                                         "' must contain only strings" );
                const std::string tag = value.get<std::string>();
                if( tag.empty() )
                    throw RegistryError( context( source, index ) + ": '" + field +
                                         "' must not contain empty strings" );
                if( std::find( result.begin(), result.end(), tag ) != result.end() )
                    throw RegistryError( context( source, index ) + ": duplicate tag '" +
                                         tag + "'" );
                result.push_back( tag );
            }
            return result;
        }
    } // namespace

    void RegistryLoader::loadFromDirectory( const std::filesystem::path &dir,
                                            BlockRegistry &blocks,
                                            BiomeRegistry &biomes,
                                            ResourceRegistry &resources )
    {
        const auto blocksPath = dir / "blocks.json";
        const auto biomesPath = dir / "biomes.json";
        const auto resourcesPath = dir / "resources.json";

        parseBlockFile( readTextFile( blocksPath, "blocks.json" ),
                        blocksPath.string(), blocks );
        parseBiomes( parseJson( readTextFile( biomesPath, "biomes.json" ),
                                biomesPath.string() ),
                     biomesPath.string(), biomes, blocks );
        parseResources( parseJson( readTextFile( resourcesPath, "resources.json" ),
                                   resourcesPath.string() ),
                        resourcesPath.string(), resources, blocks );

        // resourceId is optional biome metadata, but when present it must be
        // resolvable after the resource registry has been loaded.
        for( const std::string &biomeId : biomes.ids() )
        {
            const BiomeDef &biome = biomes.get( biomeId );
            if( !biome.resourceId.empty() && !resources.contains( biome.resourceId ) )
                throw RegistryError( biomesPath.string() + ": biome '" + biome.id +
                                     "' references unknown resourceId '" + biome.resourceId + "'" );
        }
    }

    void RegistryLoader::parseBlockFile( const std::string &text, const std::string &source,
                                         BlockRegistry &out )
    {
        parseBlocks( parseJson( text, source ), source, out );
    }

    void RegistryLoader::parseBlocks( const json &root, const std::string &source,
                                      BlockRegistry &out )
    {
        if( !root.is_object() || !root.contains( "blocks" ) || !root["blocks"].is_array() )
            throw RegistryError( source + ": expected an object with a 'blocks' array" );

        static const char *const allowed[] = {
            "id", "displayName", "tags", "solid", "transparent", "opaque", "emission",
            "renderShape", "alphaMode", "alphaCutoff", "texture", "color", "normalMap",
            "reflectionMap", "roughness", "metalness", "reflection", "transparency",
            "refraction", "indexOfRefraction", "normalMapStrength", "receiveShadows",
            "castShadows"
        };
        const size_t allowedCount = sizeof( allowed ) / sizeof( allowed[0] );

        int index = 0;
        for( const json &entry : root["blocks"] )
        {
            ++index;
            if( !entry.is_object() )
                throw RegistryError( context( source, index ) + ": expected an object" );
            checkUnknownFields( entry, source, index, allowed, allowedCount );

            BlockDef def;
            def.id = requireString( entry, source, index, "id" );
            def.displayName = requireString( entry, source, index, "displayName" );
            def.tags = optionalStringArray( entry, source, index, "tags" );

            if( entry.contains( "solid" ) )
            {
                if( !entry["solid"].is_boolean() )
                    throw RegistryError( context( source, index ) + ": 'solid' must be a boolean" );
                def.solid = entry["solid"].get<bool>();
            }
            if( entry.contains( "transparent" ) )
            {
                if( !entry["transparent"].is_boolean() )
                    throw RegistryError( context( source, index ) + ": 'transparent' must be a boolean" );
                def.transparent = entry["transparent"].get<bool>();
            }
            if( entry.contains( "opaque" ) )
            {
                if( !entry["opaque"].is_boolean() )
                    throw RegistryError( context( source, index ) + ": 'opaque' must be a boolean" );
                def.opaque = entry["opaque"].get<bool>();
            }
            if( entry.contains( "emission" ) )
            {
                if( !entry["emission"].is_number_integer() )
                    throw RegistryError( context( source, index ) + ": 'emission' must be an integer" );
                def.emission = entry["emission"].get<std::int32_t>();
                if( def.emission < 0 || def.emission > 15 )
                    throw RegistryError( context( source, index ) + ": 'emission' must be in 0..15" );
            }
            if( entry.contains( "renderShape" ) )
            {
                if( !entry["renderShape"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'renderShape' must be a string" );
                const std::string shape = entry["renderShape"].get<std::string>();
                if( shape == "cube" ) def.renderShape = BlockRenderShape::Cube;
                else if( shape == "cross" ) def.renderShape = BlockRenderShape::Cross;
                else
                    throw RegistryError( context( source, index ) +
                                         ": 'renderShape' must be 'cube' or 'cross'" );
            }
            const bool alphaModeExplicit = entry.contains( "alphaMode" );
            if( alphaModeExplicit )
            {
                if( !entry["alphaMode"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'alphaMode' must be a string" );
                const std::string mode = entry["alphaMode"].get<std::string>();
                if( mode == "opaque" ) def.alphaMode = BlockAlphaMode::Opaque;
                else if( mode == "mask" ) def.alphaMode = BlockAlphaMode::Mask;
                else if( mode == "blend" ) def.alphaMode = BlockAlphaMode::Blend;
                else
                    throw RegistryError( context( source, index ) +
                                         ": 'alphaMode' must be 'opaque', 'mask' or 'blend'" );
            }
            def.alphaCutoff = optionalFloatInRange( entry, source, index, "alphaCutoff",
                                                     def.alphaCutoff, 0.0f, 1.0f );
            def.texture = optionalNonEmptyString( entry, source, index, "texture" );
            if( entry.contains( "color" ) )
                def.color = parseColor( entry["color"], source, index );

            def.normalMap = optionalNonEmptyString( entry, source, index, "normalMap" );
            def.reflectionMap = optionalNonEmptyString( entry, source, index, "reflectionMap" );
            def.roughness = optionalFloatInRange( entry, source, index, "roughness",
                                                  def.roughness, 0.02f, 1.0f );
            def.metalness = optionalFloatInRange( entry, source, index, "metalness",
                                                  def.metalness, 0.0f, 1.0f );
            def.reflection = optionalFloatInRange( entry, source, index, "reflection",
                                                   def.reflection, 0.0f, 1.0f );
            def.transparency = optionalFloatInRange( entry, source, index, "transparency",
                                                     def.transparency, 0.0f, 1.0f );
            def.refraction = optionalFloatInRange( entry, source, index, "refraction",
                                                   def.refraction, 0.0f, 1.0f );
            def.indexOfRefraction = optionalFloatInRange( entry, source, index, "indexOfRefraction",
                                                          def.indexOfRefraction, 1.0f, 3.0f );
            def.normalMapStrength = optionalFloatInRange( entry, source, index, "normalMapStrength",
                                                          def.normalMapStrength, 0.0f, 4.0f );
            def.receiveShadows = optionalBool( entry, source, index, "receiveShadows", true );
            def.castShadows = optionalBool( entry, source, index, "castShadows", true );

            // Keep legacy data compatible while making alpha semantics explicit.
            // Masked/cutout materials use depth writes + alpha test, not alpha blending.
            if( !alphaModeExplicit )
            {
                if( def.transparency > 0.0f || def.refraction > 0.0f || def.transparent )
                    def.alphaMode = BlockAlphaMode::Blend;
            }

            if( def.alphaMode == BlockAlphaMode::Mask )
            {
                if( def.refraction > 0.0f || def.transparency > 0.0f )
                    throw RegistryError( context( source, index ) +
                                         ": alphaMode 'mask' cannot use transparency/refraction" );
                def.transparent = false; // opaque render queue; texture holes are discarded
                def.opaque = false;      // holes must not occlude neighbouring voxel faces
            }
            else if( def.alphaMode == BlockAlphaMode::Blend )
            {
                def.transparent = true;
                def.opaque = false;
            }
            else if( def.transparency > 0.0f || def.refraction > 0.0f )
            {
                throw RegistryError( context( source, index ) +
                                     ": alphaMode 'opaque' cannot use transparency/refraction" );
            }

            out.insert( def );
        }
    }

    void RegistryLoader::parseBiomes( const json &root, const std::string &source,
                                      BiomeRegistry &out, const BlockRegistry &blocks )
    {
        if( !root.is_object() || !root.contains( "biomes" ) || !root["biomes"].is_array() )
            throw RegistryError( source + ": expected an object with a 'biomes' array" );

        static const char *const allowed[] = { "id", "displayName", "surfaceBlock",
                                               "fillerBlock", "resourceId",
                                               "temperature", "rainfall", "continentalness",
                                               "weight", "selection", "terrainMaskField", "terrain" };
        const size_t allowedCount = sizeof( allowed ) / sizeof( allowed[0] );

        int index = 0;
        for( const json &entry : root["biomes"] )
        {
            ++index;
            if( !entry.is_object() )
                throw RegistryError( context( source, index ) + ": expected an object" );
            checkUnknownFields( entry, source, index, allowed, allowedCount );

            BiomeDef def;
            def.id = requireString( entry, source, index, "id" );
            def.displayName = requireString( entry, source, index, "displayName" );
            def.surfaceBlock = requireString( entry, source, index, "surfaceBlock" );
            if( !blocks.contains( def.surfaceBlock ) )
                throw RegistryError( context( source, index ) + ": surfaceBlock '" +
                                     def.surfaceBlock + "' is not a registered block" );

            if( entry.contains( "fillerBlock" ) )
            {
                if( !entry["fillerBlock"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'fillerBlock' must be a string" );
                def.fillerBlock = entry["fillerBlock"].get<std::string>();
                if( !blocks.contains( def.fillerBlock ) )
                    throw RegistryError( context( source, index ) + ": fillerBlock '" +
                                         def.fillerBlock + "' is not a registered block" );
            }
            else
            {
                // Optional means useful, not "empty id that explodes later in
                // worldgen". A biome without an explicit filler uses its
                // surface block for the shallow subsurface layers.
                def.fillerBlock = def.surfaceBlock;
            }
            if( entry.contains( "resourceId" ) )
            {
                if( !entry["resourceId"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'resourceId' must be a string" );
                def.resourceId = entry["resourceId"].get<std::string>();
            }
            if( entry.contains( "temperature" ) )
            {
                if( !entry["temperature"].is_number() )
                    throw RegistryError( context( source, index ) + ": 'temperature' must be a number" );
                def.temperature = entry["temperature"].get<double>();
                if( def.temperature < 0.0 || def.temperature > 1.0 )
                    throw RegistryError( context( source, index ) + ": 'temperature' must be in 0..1" );
            }
            if( entry.contains( "rainfall" ) )
            {
                if( !entry["rainfall"].is_number() )
                    throw RegistryError( context( source, index ) + ": 'rainfall' must be a number" );
                def.rainfall = entry["rainfall"].get<double>();
                if( def.rainfall < 0.0 || def.rainfall > 1.0 )
                    throw RegistryError( context( source, index ) + ": 'rainfall' must be in 0..1" );
            }
            if( entry.contains( "continentalness" ) )
            {
                if( !entry["continentalness"].is_number() )
                    throw RegistryError( context( source, index ) +
                                         ": 'continentalness' must be a number" );
                def.continentalness = entry["continentalness"].get<double>();
                if( def.continentalness < 0.0 || def.continentalness > 1.0 )
                    throw RegistryError( context( source, index ) +
                                         ": 'continentalness' must be in 0..1" );
            }

            if( entry.contains( "selection" ) )
            {
                const json &selection = entry["selection"];
                if( !selection.is_object() )
                    throw RegistryError( context( source, index ) + ": 'selection' must be an object" );

                static const char *const selectionAllowed[] = { "sharpness", "fields" };
                for( const auto &[key, value] : selection.items() )
                {
                    (void)value;
                    bool known = false;
                    for( const char *candidate : selectionAllowed ) known = known || key == candidate;
                    if( !known )
                        throw RegistryError( context( source, index ) +
                                             ": unknown selection field '" + key + "'" );
                }

                if( selection.contains( "sharpness" ) )
                {
                    if( !selection["sharpness"].is_number() )
                        throw RegistryError( context( source, index ) +
                                             ": selection.sharpness must be a number" );
                    def.selectionSharpness = selection["sharpness"].get<double>();
                    if( def.selectionSharpness <= 0.0 )
                        throw RegistryError( context( source, index ) +
                                             ": selection.sharpness must be > 0" );
                }

                if( selection.contains( "fields" ) )
                {
                    const json &fields = selection["fields"];
                    if( !fields.is_array() )
                        throw RegistryError( context( source, index ) +
                                             ": selection.fields must be an array" );
                    for( const json &rule : fields )
                    {
                        if( !rule.is_object() )
                            throw RegistryError( context( source, index ) +
                                                 ": selection.fields entries must be objects" );
                        static const char *const ruleAllowed[] = { "field", "min", "max", "fade" };
                        for( const auto &[key, value] : rule.items() )
                        {
                            (void)value;
                            bool known = false;
                            for( const char *candidate : ruleAllowed ) known = known || key == candidate;
                            if( !known )
                                throw RegistryError( context( source, index ) +
                                                     ": unknown selection rule field '" + key + "'" );
                        }
                        BiomeSelectionFieldDef outRule;
                        if( !rule.contains( "field" ) || !rule["field"].is_string() ||
                            rule["field"].get<std::string>().empty() )
                            throw RegistryError( context( source, index ) +
                                                 ": selection.fields.field must be a non-empty string" );
                        outRule.field = rule["field"].get<std::string>();
                        if( rule.contains( "min" ) )
                        {
                            if( !rule["min"].is_number() )
                                throw RegistryError( context( source, index ) +
                                                     ": selection.fields.min must be a number" );
                            outRule.minValue = rule["min"].get<double>();
                        }
                        if( rule.contains( "max" ) )
                        {
                            if( !rule["max"].is_number() )
                                throw RegistryError( context( source, index ) +
                                                     ": selection.fields.max must be a number" );
                            outRule.maxValue = rule["max"].get<double>();
                        }
                        if( rule.contains( "fade" ) )
                        {
                            if( !rule["fade"].is_number() )
                                throw RegistryError( context( source, index ) +
                                                     ": selection.fields.fade must be a number" );
                            outRule.fade = rule["fade"].get<double>();
                        }
                        if( outRule.maxValue < outRule.minValue )
                            throw RegistryError( context( source, index ) +
                                                 ": selection.fields.max must be >= min" );
                        if( outRule.fade < 0.0 )
                            throw RegistryError( context( source, index ) +
                                                 ": selection.fields.fade must be >= 0" );
                        def.selectionFields.push_back( std::move( outRule ) );
                    }
                }
            }

            if( entry.contains( "terrainMaskField" ) )
            {
                if( !entry["terrainMaskField"].is_string() )
                    throw RegistryError( context( source, index ) +
                                         ": 'terrainMaskField' must be a string" );
                def.terrainMaskField = entry["terrainMaskField"].get<std::string>();
            }

            if( entry.contains( "terrain" ) )
            {
                const json &terrain = entry["terrain"];
                if( !terrain.is_object() )
                    throw RegistryError( context( source, index ) + ": 'terrain' must be an object" );

                static const char *const terrainAllowed[] = {
                    "heightOffset", "heightMultiplier", "detailAmplitude", "detailScale",
                    "detailMultiplier", "ridgeAmplitude", "ridgeScale", "ridgeSharpness",
                    "islandAmplitude", "islandScale", "islandThreshold", "islandSharpness"
                };
                for( const auto &[key, value] : terrain.items() )
                {
                    (void)value;
                    bool known = false;
                    for( const char *candidate : terrainAllowed )
                        known = known || key == candidate;
                    if( !known )
                        throw RegistryError( context( source, index ) +
                                             ": unknown terrain field '" + key + "'" );
                }

                const auto readTerrainNumber = [&]( const char *name, double &target ) {
                    const auto it = terrain.find( name );
                    if( it == terrain.end() )
                        return;
                    if( !( *it ).is_number() )
                        throw RegistryError( context( source, index ) + ": terrain.'" +
                                             std::string( name ) + "' must be a number" );
                    target = ( *it ).get<double>();
                };

                readTerrainNumber( "heightOffset", def.terrain.heightOffset );
                readTerrainNumber( "heightMultiplier", def.terrain.heightMultiplier );
                readTerrainNumber( "detailAmplitude", def.terrain.detailAmplitude );
                readTerrainNumber( "detailScale", def.terrain.detailScale );
                readTerrainNumber( "detailMultiplier", def.terrain.detailMultiplier );
                readTerrainNumber( "ridgeAmplitude", def.terrain.ridgeAmplitude );
                readTerrainNumber( "ridgeScale", def.terrain.ridgeScale );
                readTerrainNumber( "ridgeSharpness", def.terrain.ridgeSharpness );
                readTerrainNumber( "islandAmplitude", def.terrain.islandAmplitude );
                readTerrainNumber( "islandScale", def.terrain.islandScale );
                readTerrainNumber( "islandThreshold", def.terrain.islandThreshold );
                readTerrainNumber( "islandSharpness", def.terrain.islandSharpness );

                if( def.terrain.heightMultiplier < 0.0 || def.terrain.detailAmplitude < 0.0 ||
                    def.terrain.detailMultiplier < 0.0 || def.terrain.ridgeAmplitude < 0.0 ||
                    def.terrain.islandAmplitude < 0.0 )
                    throw RegistryError( context( source, index ) +
                                         ": terrain multipliers/amplitudes must be >= 0" );
                if( def.terrain.detailScale <= 0.0 || def.terrain.detailScale > 1.0 ||
                    def.terrain.ridgeScale <= 0.0 || def.terrain.ridgeScale > 1.0 ||
                    def.terrain.islandScale <= 0.0 || def.terrain.islandScale > 1.0 )
                    throw RegistryError( context( source, index ) +
                                         ": terrain scales must satisfy 0 < scale <= 1" );
                if( def.terrain.ridgeSharpness <= 0.0 || def.terrain.islandSharpness <= 0.0 )
                    throw RegistryError( context( source, index ) +
                                         ": terrain sharpness values must be > 0" );
                if( def.terrain.islandThreshold < -1.0 || def.terrain.islandThreshold > 1.0 )
                    throw RegistryError( context( source, index ) +
                                         ": terrain.islandThreshold must be in -1..1" );
            }
            if( entry.contains( "weight" ) )
            {
                if( !entry["weight"].is_number() )
                    throw RegistryError( context( source, index ) + ": 'weight' must be a number" );
                def.weight = entry["weight"].get<double>();
                if( def.weight <= 0.0 )
                    throw RegistryError( context( source, index ) + ": 'weight' must be > 0" );
            }

            out.insert( def );
        }
    }

    void RegistryLoader::parseResources( const json &root, const std::string &source,
                                         ResourceRegistry &out, const BlockRegistry &blocks )
    {
        if( !root.is_object() || !root.contains( "resources" ) || !root["resources"].is_array() )
            throw RegistryError( source + ": expected an object with a 'resources' array" );

        static const char *const allowed[] = { "id", "displayName", "blockId",
                                               "weight", "chance", "minY", "maxY" };
        const size_t allowedCount = sizeof( allowed ) / sizeof( allowed[0] );

        int index = 0;
        for( const json &entry : root["resources"] )
        {
            ++index;
            if( !entry.is_object() )
                throw RegistryError( context( source, index ) + ": expected an object" );
            checkUnknownFields( entry, source, index, allowed, allowedCount );

            ResourceDef def;
            def.id = requireString( entry, source, index, "id" );
            def.displayName = requireString( entry, source, index, "displayName" );
            def.blockId = requireString( entry, source, index, "blockId" );
            if( !blocks.contains( def.blockId ) )
                throw RegistryError( context( source, index ) + ": blockId '" +
                                     def.blockId + "' is not a registered block" );

            if( entry.contains( "weight" ) )
            {
                if( !entry["weight"].is_number() )
                    throw RegistryError( context( source, index ) + ": 'weight' must be a number" );
                def.weight = entry["weight"].get<double>();
                if( def.weight < 0.0 )
                    throw RegistryError( context( source, index ) + ": 'weight' must be >= 0" );
            }
            if( entry.contains( "chance" ) )
            {
                if( !entry["chance"].is_number() )
                    throw RegistryError( context( source, index ) + ": 'chance' must be a number" );
                def.chance = entry["chance"].get<double>();
                if( def.chance < 0.0 || def.chance > 1.0 )
                    throw RegistryError( context( source, index ) + ": 'chance' must be in 0..1" );
            }
            if( entry.contains( "minY" ) )
            {
                if( !entry["minY"].is_number_integer() )
                    throw RegistryError( context( source, index ) + ": 'minY' must be an integer" );
                def.minY = entry["minY"].get<std::int32_t>();
            }
            if( entry.contains( "maxY" ) )
            {
                if( !entry["maxY"].is_number_integer() )
                    throw RegistryError( context( source, index ) + ": 'maxY' must be an integer" );
                def.maxY = entry["maxY"].get<std::int32_t>();
            }
            if( def.minY > def.maxY )
                throw RegistryError( context( source, index ) + ": 'minY' > 'maxY'" );

            out.insert( def );
        }
    }

    std::string RegistryLoader::requireString( const json &object, const std::string &source,
                                               int index, const char *field )
    {
        if( !object.contains( field ) )
            throw RegistryError( context( source, index ) + ": missing required field '" +
                                 std::string( field ) + "'" );
        if( !object[field].is_string() )
            throw RegistryError( context( source, index ) + ": '" + std::string( field ) +
                                 "' must be a string" );
        return object[field].get<std::string>();
    }

    void RegistryLoader::checkUnknownFields( const json &object, const std::string &source,
                                             int index, const char *const allowed[], size_t count )
    {
        for( auto it = object.begin(); it != object.end(); ++it )
        {
            const std::string &key = it.key();
            bool known = false;
            for( size_t a = 0; a < count; ++a )
            {
                if( key == allowed[a] )
                {
                    known = true;
                    break;
                }
            }
            if( !known )
                throw RegistryError( context( source, index ) + ": unknown field '" + key +
                                     "' is not allowed here" );
        }
    }
} // namespace world
