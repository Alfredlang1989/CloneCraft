#include "world/registry/RegistryLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
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

        /**
         * Checked JSON -> uint32 read (M01-A): the full 64-bit source integer
         * is range-validated BEFORE narrowing, so an out-of-range value can
         * never wrap into a small valid-looking number (e.g. bitWidth
         * 4294967297 must not become 1, uint32 defaultValue 4294967296 must
         * not become 0). Semantic bounds of the field (bitWidth 1..32,
         * valueType max, serializationVersion >= 1, ...) are still applied by
         * the call site after this read. This is the single checked
         * unsigned-32 parsing path for the registry JSON boundary.
         *
         * nlohmann quirk (reviewer round 2): is_number_integer() is true for
         * BOTH signed and unsigned integer storage, so the unsigned branch
         * must come first - otherwise a huge uint64 would be read via
         * get<int64_t>() and wrap into a legal-looking negative/small value.
         */
        std::uint32_t requireUint32( const json &value, const std::string &where,
                                     const std::string &field )
        {
            if( value.is_number_unsigned() )
            {
                const std::uint64_t v = value.get<std::uint64_t>();
                if( v > 0xFFFFFFFFull )
                    throw RegistryError( where + ": '" + field + "' exceeds the uint32 range" );
                return static_cast<std::uint32_t>( v );
            }
            if( value.is_number_integer() )
            {
                const std::int64_t v = value.get<std::int64_t>();
                if( v < 0 )
                    throw RegistryError( where + ": '" + field + "' must not be negative" );
                if( static_cast<std::uint64_t>( v ) > 0xFFFFFFFFull )
                    throw RegistryError( where + ": '" + field + "' exceeds the uint32 range" );
                return static_cast<std::uint32_t>( v );
            }
            throw RegistryError( where + ": '" + field + "' must be an unsigned integer" );
        }

        /**
         * Checked JSON -> int32 read (M01-A): the same pre-narrowing range
         * check for the signed registry fields (color channels, emission,
         * minY/maxY), so values outside int32 can never wrap into the signed
         * domain. Same nlohmann quirk: the unsigned branch is checked first,
         * otherwise UINT64_MAX read via get<int64_t>() would become -1 and
         * pass the signed range check. Call sites keep their semantic bounds
         * (0..255, 0..15, ...).
         */
        std::int32_t requireInt32( const json &value, const std::string &where,
                                   const std::string &field )
        {
            if( value.is_number_unsigned() )
            {
                const std::uint64_t v = value.get<std::uint64_t>();
                if( v > static_cast<std::uint64_t>( std::numeric_limits<std::int32_t>::max() ) )
                    throw RegistryError( where + ": '" + field + "' exceeds the int32 range" );
                return static_cast<std::int32_t>( v );
            }
            if( value.is_number_integer() )
            {
                const std::int64_t v = value.get<std::int64_t>();
                if( v < static_cast<std::int64_t>( std::numeric_limits<std::int32_t>::min() ) ||
                    v > static_cast<std::int64_t>( std::numeric_limits<std::int32_t>::max() ) )
                    throw RegistryError( where + ": '" + field + "' is outside the int32 range" );
                return static_cast<std::int32_t>( v );
            }
            throw RegistryError( where + ": '" + field + "' must be an integer" );
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
                    const int channel = requireInt32( value[i], where, "color channel" );
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
            "castShadows", "visualTintProperty"
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
            if( !isNamespacedId( def.id ) )
                throw RegistryError( context( source, index ) +
                                     ": block id '" + def.id +
                                     "' must be namespaced as <namespace>:<name>" );
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
                def.emission = requireInt32( entry["emission"], context( source, index ),
                                             "emission" );
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
            def.visualTintProperty =
                optionalNonEmptyString( entry, source, index, "visualTintProperty" );
            if( !def.visualTintProperty.empty() && !isNamespacedId( def.visualTintProperty ) )
                throw RegistryError( context( source, index ) +
                                     ": 'visualTintProperty' must be namespaced as "
                                     "<namespace>:<name>" );

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
            if( !isNamespacedId( def.id ) )
                throw RegistryError( context( source, index ) +
                                     ": biome id '" + def.id +
                                     "' must be namespaced as <namespace>:<name>" );
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
            if( !isNamespacedId( def.id ) )
                throw RegistryError( context( source, index ) +
                                     ": resource id '" + def.id +
                                     "' must be namespaced as <namespace>:<name>" );
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
                def.minY = requireInt32( entry["minY"], context( source, index ), "minY" );
            }
            if( entry.contains( "maxY" ) )
            {
                def.maxY = requireInt32( entry["maxY"], context( source, index ), "maxY" );
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

    void RegistryLoader::parsePrototypes( const json &root, const std::string &source,
                                          const BlockRegistry &blocks, PrototypeRegistry &out,
                                          const SidecarRegistry *sidecars )
    {
        if( !root.is_object() || !root.contains( "prototypes" ) || !root["prototypes"].is_array() )
            throw RegistryError( source + ": expected an object with a 'prototypes' array" );

        static const char *const allowed[] = {
            "id", "displayName", "blockId", "capabilities", "properties"
        };
        const size_t allowedCount = sizeof( allowed ) / sizeof( allowed[0] );

        int index = 0;
        for( const json &entry : root["prototypes"] )
        {
            ++index;
            if( !entry.is_object() )
                throw RegistryError( context( source, index ) + ": expected an object" );
            checkUnknownFields( entry, source, index, allowed, allowedCount );

            PrototypeDef def;
            def.id = requireString( entry, source, index, "id" );
            if( !isNamespacedId( def.id ) )
                throw RegistryError( context( source, index ) +
                                     ": prototype id '" + def.id +
                                     "' must be namespaced as <namespace>:<name>" );
            def.displayName = requireString( entry, source, index, "displayName" );
            def.blockId = requireString( entry, source, index, "blockId" );
            def.capabilities = optionalStringArray( entry, source, index, "capabilities" );

            std::vector<std::string> seen;
            for( const std::string &capability : def.capabilities )
            {
                if( std::find( seen.begin(), seen.end(), capability ) != seen.end() )
                    throw RegistryError( context( source, index ) +
                                         ": duplicate capability '" + capability + "'" );
                seen.push_back( capability );
            }

            if( entry.contains( "properties" ) )
            {
                if( !entry["properties"].is_array() )
                    throw RegistryError( context( source, index ) +
                                         ": 'properties' must be an array" );
                static const char *const propertyAllowed[] = { "id", "defaultValue" };
                const size_t propertyAllowedCount =
                    sizeof( propertyAllowed ) / sizeof( propertyAllowed[0] );
                std::vector<std::string> seenProperties;
                int propertyIndex = 0;
                for( const json &property : entry["properties"] )
                {
                    ++propertyIndex;
                    if( !property.is_object() )
                        throw RegistryError( context( source, index ) +
                                             ": 'properties' entry " +
                                             std::to_string( propertyIndex ) +
                                             " must be an object" );
                    checkUnknownFields( property, source, index, propertyAllowed,
                                        propertyAllowedCount );
                    PrototypePropertyDef propertyDef;
                    propertyDef.id = requireString( property, source, index, "id" );
                    if( !isNamespacedId( propertyDef.id ) )
                        throw RegistryError( context( source, index ) +
                                             ": property id '" + propertyDef.id +
                                             "' must be namespaced as <namespace>:<name>" );
                    if( std::find( seenProperties.begin(), seenProperties.end(),
                                   propertyDef.id ) != seenProperties.end() )
                        throw RegistryError( context( source, index ) +
                                             ": duplicate property '" + propertyDef.id +
                                             "' in prototype '" + def.id + "'" );
                    seenProperties.push_back( propertyDef.id );
                    if( !property.contains( "defaultValue" ) )
                        throw RegistryError( context( source, index ) +
                                             ": property '" + propertyDef.id +
                                             "' requires 'defaultValue'" );
                    const json &defaultValue = property["defaultValue"];
                    // M01-A numeric rule (ADR-029 follow-up): a prototype
                    // property default is validated against the declared
                    // sidecar type when one is registered, with one shared
                    // documented JSON number rule:
                    //  - float sidecar types accept any JSON number; integer
                    //    syntax denotes the exact integral value, so `0` and
                    //    `0.0` are the same default (mirrors the sidecars.json
                    //    float rule).
                    //  - integer sidecar types require unsigned integer
                    //    syntax, and the full int64 is range-checked *before*
                    //    narrowing: -1 must never wrap to 4294967295 and
                    //    4294967296 must never truncate to 0.
                    //    Legacy/programmatic parses without a sidecar registry
                    //    apply the same range rule; the runtime WorldState
                    //    still guards has()/get()/set() consistency (ADR-029).
                    {
                        const SidecarDef *declaredSidecar =
                            sidecars ? sidecars->find( propertyDef.id ) : nullptr;
                        if( declaredSidecar &&
                            declaredSidecar->valueType == SidecarValueType::Float )
                        {
                            if( !defaultValue.is_number() )
                                throw RegistryError( context( source, index ) +
                                                     ": property '" + propertyDef.id +
                                                     "' defaultValue must be a number for a float sidecar type" );
                            propertyDef.defaultValue =
                                PropertyValue{ defaultValue.get<float>() };
                        }
                        else if( defaultValue.is_number_integer() ||
                                 defaultValue.is_number_unsigned() )
                        {
                            // Same checked unsigned-first path as the sidecar
                            // fields: the full source integer is range-checked
                            // before narrowing (is_number_integer() covers
                            // unsigned storage too, so a direct
                            // get<int64_t>() here could wrap UINT64_MAX into
                            // -1 and then pass the range check).
                            propertyDef.defaultValue =
                                PropertyValue{ requireUint32( defaultValue,
                                                              context( source, index ),
                                                              "defaultValue" ) };
                        }
                        else if( defaultValue.is_number_float() )
                        {
                            // A declared integer sidecar rejects float syntax
                            // (one numeric rule per type). Without a sidecar
                            // registry the type is unknown; the legacy path
                            // keeps the float default as-is and the runtime
                            // WorldState still gates it (ADR-029).
                            if( declaredSidecar )
                                throw RegistryError( context( source, index ) +
                                                     ": property '" + propertyDef.id +
                                                     "' defaultValue must be an integer for an integer sidecar type" );
                            propertyDef.defaultValue =
                                PropertyValue{ defaultValue.get<float>() };
                        }
                        else
                            throw RegistryError( context( source, index ) +
                                                 ": property '" + propertyDef.id +
                                                 "' defaultValue must be a number" );
                    }

                    // M05 round 2: prototype properties are validated against
                    // sidecars.json at load time (ADR-027). A property without
                    // a backing sidecar type, or a default that cannot be
                    // stored by that type, is a broken mod - reject it instead
                    // of producing the has()==true / get()==nullopt
                    // schizophrenia later.
                    if( sidecars )
                    {
                        const SidecarDef *sidecar = sidecars->find( propertyDef.id );
                        if( !sidecar )
                            throw RegistryError( context( source, index ) +
                                                 ": prototype '" + def.id +
                                                 "' declares property '" + propertyDef.id +
                                                 "' which is not a registered sidecar type" );
                        // M01-B scope contract (#20): a prototype property is
                        // per-block state, so it must reference a
                        // block-scoped sidecar type. Runtime WorldState and
                        // the loader share this identical rule.
                        if( sidecar->scope != SidecarScope::Block )
                            throw RegistryError( context( source, index ) +
                                                 ": prototype '" + def.id +
                                                 "' property '" + propertyDef.id +
                                                 "' references sidecar type '" +
                                                 propertyDef.id +
                                                 "' which is not block-scoped" );
                        if( !world::valueFitsSidecarDef( *sidecar, propertyDef.defaultValue ) )
                        {
                            const std::string valueText =
                                std::holds_alternative<std::uint32_t>( propertyDef.defaultValue )
                                    ? std::to_string( std::get<std::uint32_t>( propertyDef.defaultValue ) )
                                    : std::to_string( std::get<float>( propertyDef.defaultValue ) );
                            throw RegistryError( context( source, index ) +
                                                 ": prototype '" + def.id +
                                                 "' property '" + propertyDef.id +
                                                 "' default " + valueText +
                                                 " does not fit sidecar type '" + propertyDef.id + "'" );
                        }
                    }
                    def.properties.push_back( std::move( propertyDef ) );
                }
            }

            if( !blocks.contains( def.blockId ) )
                throw RegistryError( context( source, index ) + ": prototype '" + def.id +
                                     "' references unknown blockId '" + def.blockId + "'" );

            // Claim check runs against the registry's persistent index, so a
            // second parse call into the same registry cannot smuggle in a
            // second prototype for an already claimed block either.
            if( const std::string *owner = out.claimedBy( def.blockId ) )
                throw RegistryError( context( source, index ) + ": prototype '" + def.id +
                                     "' claims blockId '" + def.blockId +
                                     "' which is already claimed by prototype '" + *owner + "'" );

            out.insert( def );
        }
    }

    bool RegistryLoader::loadPrototypes( const std::filesystem::path &dir,
                                         const BlockRegistry &blocks,
                                         PrototypeRegistry &out,
                                         const SidecarRegistry *sidecars )
    {
        const auto prototypesPath = dir / "prototypes.json";
        std::error_code error;
        if( !std::filesystem::exists( prototypesPath, error ) || error )
            return false;

        parsePrototypes( parseJson( readTextFile( prototypesPath, "prototypes.json" ),
                                    prototypesPath.string() ),
                         prototypesPath.string(), blocks, out, sidecars );
        return true;
    }

    void RegistryLoader::parseSidecars( const json &root, const std::string &source,
                                        SidecarRegistry &out )
    {
        if( !root.is_object() || !root.contains( "sidecars" ) || !root["sidecars"].is_array() )
            throw RegistryError( source + ": expected an object with a 'sidecars' array" );

        static const char *const allowed[] = {
            "id", "displayName", "valueType", "scope", "defaultValue", "bitWidth",
            "storage", "persist", "serializationVersion"
        };
        const size_t allowedCount = sizeof( allowed ) / sizeof( allowed[0] );

        int index = 0;
        for( const json &entry : root["sidecars"] )
        {
            ++index;
            if( !entry.is_object() )
                throw RegistryError( context( source, index ) + ": expected an object" );
            checkUnknownFields( entry, source, index, allowed, allowedCount );

            SidecarDef def;
            def.id = requireString( entry, source, index, "id" );
            if( !isNamespacedId( def.id ) )
                throw RegistryError( context( source, index ) +
                                     ": sidecar id '" + def.id +
                                     "' must be namespaced as <namespace>:<name>" );
            def.displayName = requireString( entry, source, index, "displayName" );

            // M01-B scope contract (#20): 'scope' is a mandatory field of every
            // registered sidecar type. A mod file may never guess its scope
            // implicitly; unknown scope strings are a hard RegistryError.
            // (The C++ SidecarDef{} default remains Block for programmatic
            // fixtures only - data files must be explicit.)
            if( !entry.contains( "scope" ) )
                throw RegistryError( context( source, index ) +
                                     ": sidecar '" + def.id +
                                     "' requires a 'scope' field" );
            if( !entry["scope"].is_string() )
                throw RegistryError( context( source, index ) +
                                     ": 'scope' must be a string" );
            {
                const std::string scope = entry["scope"].get<std::string>();
                if( scope == "block" ) def.scope = SidecarScope::Block;
                else if( scope == "chunk" ) def.scope = SidecarScope::Chunk;
                else if( scope == "chunk_group" ) def.scope = SidecarScope::ChunkGroup;
                else if( scope == "section" ) def.scope = SidecarScope::Section;
                else if( scope == "region" ) def.scope = SidecarScope::Region;
                else if( scope == "sector" ) def.scope = SidecarScope::Sector;
                else
                    throw RegistryError( context( source, index ) +
                                         ": 'scope' must be one of 'block', 'chunk', "
                                         "'chunk_group', 'section', 'region', 'sector'" );
            }

            if( entry.contains( "valueType" ) )
            {
                if( !entry["valueType"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'valueType' must be a string" );
                const std::string type = entry["valueType"].get<std::string>();
                if( type == "uint8" ) def.valueType = SidecarValueType::Uint8;
                else if( type == "uint16" ) def.valueType = SidecarValueType::Uint16;
                else if( type == "uint32" ) def.valueType = SidecarValueType::Uint32;
                else if( type == "float" ) def.valueType = SidecarValueType::Float;
                else
                    throw RegistryError( context( source, index ) +
                                         ": 'valueType' must be 'uint8', 'uint16', 'uint32' or 'float'" );
            }

            if( entry.contains( "bitWidth" ) )
            {
                def.bitWidth = requireUint32( entry["bitWidth"], context( source, index ),
                                              "bitWidth" );
                // Shared semantic with the runtime (validateSidecarDef):
                // 0 = full type width, 1..32 explicit compact width.
                if( def.bitWidth > 32u )
                    throw RegistryError( context( source, index ) +
                                         ": 'bitWidth' must be in 1..32 (0 = full type width)" );
                // Identical to the shared validator: an EXPLICIT compact
                // width (bitWidth != 0) never applies to float value types;
                // bitWidth 0 ("full type width") is valid for float too.
                if( def.valueType == SidecarValueType::Float && def.bitWidth != 0u )
                    throw RegistryError( context( source, index ) +
                                         ": 'bitWidth' only applies to integer value types" );
            }

            if( entry.contains( "defaultValue" ) )
            {
                const std::string where = context( source, index );
                if( def.valueType == SidecarValueType::Float )
                {
                    if( !entry["defaultValue"].is_number() )
                        throw RegistryError( where + ": 'defaultValue' must be a number for 'float'" );
                    def.defaultValue = entry["defaultValue"].get<float>();
                }
                else
                {
                    const std::uint32_t value =
                        requireUint32( entry["defaultValue"], where, "defaultValue" );

                    const std::uint32_t typeMax = def.valueType == SidecarValueType::Uint8   ? 255u
                                                  : def.valueType == SidecarValueType::Uint16 ? 65535u
                                                                                              : 0xFFFFFFFFu;
                    if( value > typeMax )
                        throw RegistryError( where + ": 'defaultValue' " + std::to_string( value ) +
                                             " does not fit valueType '" +
                                             ( def.valueType == SidecarValueType::Uint8   ? "uint8"
                                               : def.valueType == SidecarValueType::Uint16 ? "uint16"
                                                                                           : "uint32" ) +
                                             "'" );

                    if( def.bitWidth != 0u && def.bitWidth < 32u )
                    {
                        const std::uint32_t bitMax = ( 1u << def.bitWidth ) - 1u;
                        if( value > bitMax )
                            throw RegistryError( where + ": 'defaultValue' " +
                                                 std::to_string( value ) +
                                                 " does not fit in 'bitWidth' " +
                                                 std::to_string( def.bitWidth ) + " (max " +
                                                 std::to_string( bitMax ) + ")" );
                    }
                    def.defaultValue = value;
                }
            }

            if( entry.contains( "storage" ) )
            {
                if( !entry["storage"].is_string() )
                    throw RegistryError( context( source, index ) + ": 'storage' must be a string" );
                const std::string storage = entry["storage"].get<std::string>();
                if( storage == "sparse" ) def.storage = SidecarStorageStrategy::Sparse;
                else if( storage == "dense" ) def.storage = SidecarStorageStrategy::Dense;
                else
                    throw RegistryError( context( source, index ) +
                                         ": 'storage' must be 'sparse' or 'dense'" );
                // M01-B (#20): hierarchy sidecars are sparse by contract.
                // A dense declaration above the block tier must fail loudly
                // instead of pretending the engine implements dense stores.
                if( def.scope != SidecarScope::Block &&
                    def.storage == SidecarStorageStrategy::Dense )
                    throw RegistryError( context( source, index ) +
                                         ": 'storage' 'dense' is not supported for sidecar "
                                         "scopes above 'block' (hierarchy sidecars are sparse)" );
            }

            if( entry.contains( "persist" ) )
            {
                if( !entry["persist"].is_boolean() )
                    throw RegistryError( context( source, index ) + ": 'persist' must be a boolean" );
                def.persist = entry["persist"].get<bool>();
            }

            if( entry.contains( "serializationVersion" ) )
            {
                def.serializationVersion =
                    requireUint32( entry["serializationVersion"], context( source, index ),
                                   "serializationVersion" );
                if( def.serializationVersion == 0u )
                    throw RegistryError( context( source, index ) +
                                         ": 'serializationVersion' must be >= 1" );
            }

            // M01-B review round 4: the loader and the runtime share the
            // SAME structural validation (world::validateSidecarDef); the
            // insert gate below would reject the same definition anyway.
            // The explicit call keeps the precise field context in the
            // error message.
            world::validateSidecarDef( def, context( source, index ) );

            out.insert( def );
        }
    }

    bool RegistryLoader::loadSidecars( const std::filesystem::path &dir,
                                       SidecarRegistry &out )
    {
        const auto sidecarsPath = dir / "sidecars.json";
        std::error_code error;
        if( !std::filesystem::exists( sidecarsPath, error ) || error )
            return false;

        parseSidecars( parseJson( readTextFile( sidecarsPath, "sidecars.json" ),
                                  sidecarsPath.string() ),
                       sidecarsPath.string(), out );
        return true;
    }
} // namespace world
