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

        PassStage parsePassStage( const std::string &value, const std::filesystem::path &path,
                                  const std::string &scope )
        {
            if( value == "terrain" ) return PassStage::Terrain;
            if( value == "addon" ) return PassStage::Addon;
            configError( path, "field '" + scope + ".stage' must be 'terrain' or 'addon'" );
        }

        DecorationType parseDecorationType( const std::string &value,
                                             const std::filesystem::path &path,
                                             const std::string &scope )
        {
            if( value == "scatter" ) return DecorationType::Scatter;
            if( value == "column" ) return DecorationType::Column;
            if( value == "structure" ) return DecorationType::Structure;
            configError( path, "field '" + scope + ".type' has unknown decoration type '" +
                               value + "'" );
        }

        CompareOp parseCompareOp( const std::string &value, const std::filesystem::path &path,
                                  const std::string &scope )
        {
            if( value == "always" ) return CompareOp::Always;
            if( value == "gt" ) return CompareOp::Greater;
            if( value == "gte" ) return CompareOp::GreaterEqual;
            if( value == "lt" ) return CompareOp::Less;
            if( value == "lte" ) return CompareOp::LessEqual;
            if( value == "between" ) return CompareOp::Between;
            configError( path, "field '" + scope + ".op' has unknown comparison '" + value + "'" );
        }

    } // namespace

    WorldGenConfig loadWorldGenConfig( const std::filesystem::path &path )
    {
        std::ifstream input( path );
        if( !input )
            configError( path, "could not open file" );

        json root;
        try
        {
            input >> root;
        }
        catch( const json::parse_error &e )
        {
            configError( path, "JSON parse error at byte " + std::to_string( e.byte ) );
        }
        if( !root.is_object() )
            configError( path, "root must be an object" );

        constexpr std::array rootAllowed{
            std::string_view( "seed" ), std::string_view( "workerThreads" ),
            std::string_view( "surfaceField" ), std::string_view( "fields" ),
            std::string_view( "passes" ), std::string_view( "anchorSets" ),
            std::string_view( "decorations" )
        };
        checkUnknownFields( root, rootAllowed, path, "worldgen" );

        WorldGenConfig cfg;
        readOptional( root, "seed", cfg.seed, path, "worldgen" );
        readOptional( root, "workerThreads", cfg.workerThreads, path, "worldgen" );
        readOptional( root, "surfaceField", cfg.surfaceField, path, "worldgen" );
        if( cfg.surfaceField.empty() )
            configError( path, "field 'worldgen.surfaceField' must not be empty" );


        const auto fieldsIt = root.find( "fields" );
        if( fieldsIt == root.end() || !fieldsIt->is_array() )
            configError( path, "field 'worldgen.fields' must be an array" );

        std::unordered_set<std::string> fieldIds;
        std::unordered_map<std::string, FieldDimension> fieldDimensions;
        std::size_t index = 0;
        for( const json &entry : *fieldsIt )
        {
            ++index;
            const std::string scope = "fields[" + std::to_string( index - 1 ) + "]";
            constexpr std::array allowed{
                std::string_view( "id" ), std::string_view( "dimension" ),
                std::string_view( "script" ), std::string_view( "function" ),
                std::string_view( "salt" )
            };
            checkUnknownFields( entry, allowed, path, scope );

            FieldConfig field;
            field.id = requireString( entry, "id", path, scope );
            if( !fieldIds.insert( field.id ).second )
                configError( path, scope + ": duplicate field id '" + field.id + "'" );
            field.dimension = parseDimension( requireString( entry, "dimension", path, scope ),
                                              path, scope );
            fieldDimensions[field.id] = field.dimension;
            const std::string script = requireString( entry, "script", path, scope );
            field.scriptPath = path.parent_path() / script;
            readOptional( entry, "function", field.functionName, path, scope );
            readOptional( entry, "salt", field.salt, path, scope );
            if( field.functionName.empty() )
                configError( path, scope + ": function must not be empty" );
            cfg.fields.push_back( std::move( field ) );
        }
        if( cfg.fields.empty() )
            configError( path, "worldgen.fields must contain at least one field" );
        if( fieldIds.count( cfg.surfaceField ) == 0 )
            configError( path, "surfaceField references unknown field '" + cfg.surfaceField + "'" );
        if( fieldDimensions.at( cfg.surfaceField ) != FieldDimension::D2 )
            configError( path, "surfaceField must reference a 2d field" );

        const auto passesIt = root.find( "passes" );
        if( passesIt == root.end() || !passesIt->is_array() )
            configError( path, "field 'worldgen.passes' must be an array" );

        std::unordered_set<std::string> passIds;
        index = 0;
        for( const json &entry : *passesIt )
        {
            ++index;
            const std::string scope = "passes[" + std::to_string( index - 1 ) + "]";
            constexpr std::array allowed{
                std::string_view( "id" ), std::string_view( "type" ),
                std::string_view( "stage" ), std::string_view( "block" ),
                std::string_view( "priority" ),
                std::string_view( "field" ), std::string_view( "maskField" ),
                std::string_view( "surfaceField" ), std::string_view( "thicknessField" ),
                std::string_view( "bottomField" ), std::string_view( "thickness" ),
                std::string_view( "surfaceOffset" ), std::string_view( "bottomOffset" ),
                std::string_view( "condition" ), std::string_view( "maskCondition" ),
                std::string_view( "replaceBlocks" ),
                std::string_view( "replaceTags" )
            };
            checkUnknownFields( entry, allowed, path, scope );

            PassConfig pass;
            pass.id = requireString( entry, "id", path, scope );
            if( !passIds.insert( pass.id ).second )
                configError( path, scope + ": duplicate pass id '" + pass.id + "'" );
            pass.type = parsePassType( requireString( entry, "type", path, scope ), path, scope );
            if( const auto stageIt = entry.find( "stage" ); stageIt != entry.end() )
            {
                if( !stageIt->is_string() )
                    configError( path, scope + ".stage must be a string" );
                pass.stage = parsePassStage( stageIt->get<std::string>(), path, scope );
            }
            pass.blockId = requireString( entry, "block", path, scope );
            readOptional( entry, "priority", pass.priority, path, scope );
            readOptional( entry, "field", pass.field, path, scope );
            readOptional( entry, "maskField", pass.maskField, path, scope );
            readOptional( entry, "surfaceField", pass.surfaceField, path, scope );
            readOptional( entry, "thicknessField", pass.thicknessField, path, scope );
            readOptional( entry, "bottomField", pass.bottomField, path, scope );
            readOptional( entry, "thickness", pass.thickness, path, scope );
            readOptional( entry, "surfaceOffset", pass.surfaceOffset, path, scope );
            readOptional( entry, "bottomOffset", pass.bottomOffset, path, scope );
            pass.replaceBlocks = readStringArray( entry, "replaceBlocks", path, scope );
            pass.replaceTags = readStringArray( entry, "replaceTags", path, scope );

            const auto parseCondition = [&]( const char *name, FieldCondition &target ) {
                if( const auto condition = optionalObject( entry, name, path, scope ) )
                {
                    constexpr std::array conditionAllowed{
                        std::string_view( "op" ), std::string_view( "value" ),
                        std::string_view( "max" )
                    };
                    const std::string conditionScope = scope + "." + name;
                    checkUnknownFields( *condition, conditionAllowed, path, conditionScope );
                    const auto opIt = condition->find( "op" );
                    if( opIt != condition->end() )
                    {
                        if( !opIt->is_string() )
                            configError( path, conditionScope + ".op must be a string" );
                        target.op = parseCompareOp( opIt->get<std::string>(), path,
                                                    conditionScope );
                    }
                    readOptional( *condition, "value", target.value, path, conditionScope );
                    readOptional( *condition, "max", target.maxValue, path, conditionScope );
                    if( target.op == CompareOp::Between && target.maxValue < target.value )
                        configError( path, conditionScope + ".max must be >= value" );
                }
            };
            parseCondition( "condition", pass.condition );
            parseCondition( "maskCondition", pass.maskCondition );

            auto requireField = [&]( const std::string &id, const char *role,
                                     FieldDimension expected ) {
                if( id.empty() )
                    configError( path, scope + ": missing required " + role );
                const auto it = fieldDimensions.find( id );
                if( it == fieldDimensions.end() )
                {
                    std::string message = scope;
                    message.append( ": unknown field '" ).append( id ).push_back( '\'' );
                    configError( path, message );
                }
                if( it->second != expected )
                {
                    std::string message = scope;
                    message.append( ": " ).append( role ).append( " field '" )
                        .append( id ).append( "' has wrong dimension" );
                    configError( path, message );
                }
            };

            if( !pass.maskField.empty() )
                requireField( pass.maskField, "maskField", FieldDimension::D2 );

            switch( pass.type )
            {
            case PassType::FillBelow:
            case PassType::Surface:
                requireField( pass.field, "field", FieldDimension::D2 );
                break;
            case PassType::Volume:
                requireField( pass.field, "field", FieldDimension::D3 );
                break;
            case PassType::SurfaceLayer:
                requireField( pass.surfaceField, "surfaceField", FieldDimension::D2 );
                if( !pass.thicknessField.empty() )
                    requireField( pass.thicknessField, "thicknessField", FieldDimension::D2 );
                if( !pass.bottomField.empty() )
                    requireField( pass.bottomField, "bottomField", FieldDimension::D2 );
                if( !pass.bottomField.empty() && !pass.thicknessField.empty() )
                    configError( path, scope + ": bottomField and thicknessField are mutually exclusive" );
                if( pass.bottomField.empty() && pass.bottomOffset != 0 )
                    configError( path, scope + ": bottomOffset requires bottomField" );
                if( pass.bottomField.empty() && pass.thicknessField.empty() && pass.thickness < 1 )
                    configError( path, scope + ": thickness must be >= 1" );
                break;
            }

            cfg.passes.push_back( std::move( pass ) );
        }
        if( cfg.passes.empty() )
            configError( path, "worldgen.passes must contain at least one pass" );

        // Decoration anchors are deliberately separate from block-mutating passes.
        // Their immutable placement may be computed early for safe sky-chunk
        // reach tests; decoration proposals are applied only after the hard
        // terrain -> addon stage barrier has completed.
        std::unordered_set<std::string> anchorSetIds;
        if( const auto anchorIt = root.find( "anchorSets" ); anchorIt != root.end() )
        {
            if( !anchorIt->is_array() )
                configError( path, "field 'worldgen.anchorSets' must be an array" );
            std::size_t anchorIndex = 0;
            for( const json &entry : *anchorIt )
            {
                const std::string scope = "anchorSets[" + std::to_string( anchorIndex++ ) + "]";
                constexpr std::array allowed{
                    std::string_view( "id" ), std::string_view( "surfaceField" ),
                    std::string_view( "surfaceMode" ), std::string_view( "densityField" ),
                    std::string_view( "surfaceOffset" ), std::string_view( "spacing" ),
                    std::string_view( "maxSurfaceDrop" ), std::string_view( "chance" ),
                    std::string_view( "salt" ), std::string_view( "conditions" )
                };
                checkUnknownFields( entry, allowed, path, scope );

                AnchorSetConfig anchor;
                anchor.id = requireString( entry, "id", path, scope );
                if( !anchorSetIds.insert( anchor.id ).second )
                    configError( path, scope + ": duplicate anchor set id '" + anchor.id + "'" );
                readOptional( entry, "surfaceField", anchor.surfaceField, path, scope );
                if( const auto modeIt = entry.find( "surfaceMode" ); modeIt != entry.end() )
                {
                    if( !modeIt->is_string() )
                        configError( path, scope + ".surfaceMode must be a string" );
                    const std::string mode = modeIt->get<std::string>();
                    if( mode == "field" ) anchor.surfaceMode = AnchorSurfaceMode::Field;
                    else if( mode == "postprocess" ) anchor.surfaceMode = AnchorSurfaceMode::Postprocess;
                    else configError( path, scope + ".surfaceMode must be 'field' or 'postprocess'" );
                }
                readOptional( entry, "densityField", anchor.densityField, path, scope );
                readOptional( entry, "surfaceOffset", anchor.surfaceOffset, path, scope );
                readOptional( entry, "spacing", anchor.spacing, path, scope );
                readOptional( entry, "maxSurfaceDrop", anchor.maxSurfaceDrop, path, scope );
                readOptional( entry, "chance", anchor.chance, path, scope );
                readOptional( entry, "salt", anchor.salt, path, scope );
                if( anchor.spacing < 1 )
                    configError( path, scope + ": spacing must be >= 1" );
                if( anchor.maxSurfaceDrop < 0 )
                    configError( path, scope + ": maxSurfaceDrop must be >= 0" );
                if( !std::isfinite( anchor.chance ) || anchor.chance < 0.0 || anchor.chance > 1.0 )
                    configError( path, scope + ": chance must be in 0..1" );

                auto require2D = [&]( const std::string &id, const char *role ) {
                    if( id.empty() )
                    {
                        std::string message = scope;
                        message.append( ": missing required " ).append( role );
                        configError( path, message );
                    }
                    const auto it = fieldDimensions.find( id );
                    if( it == fieldDimensions.end() )
                    {
                        std::string message = scope;
                        message.append( ": unknown " ).append( role ).append( " field '" );
                        message.append( id ).push_back( '\'' );
                        configError( path, message );
                    }
                    if( it->second != FieldDimension::D2 )
                    {
                        std::string message = scope;
                        message.append( ": " ).append( role ).append( " field must be 2d" );
                        configError( path, message );
                    }
                };
                require2D( anchor.surfaceField, "surfaceField" );
                if( !anchor.densityField.empty() ) require2D( anchor.densityField, "densityField" );

                if( const auto conditionsIt = entry.find( "conditions" ); conditionsIt != entry.end() )
                {
                    if( !conditionsIt->is_array() )
                        configError( path, scope + ".conditions must be an array" );
                    std::size_t conditionIndex = 0;
                    for( const json &conditionEntry : *conditionsIt )
                    {
                        const std::string conditionScope = scope + ".conditions[" +
                            std::to_string( conditionIndex++ ) + "]";
                        constexpr std::array conditionAllowed{
                            std::string_view( "field" ), std::string_view( "op" ),
                            std::string_view( "value" ), std::string_view( "max" )
                        };
                        checkUnknownFields( conditionEntry, conditionAllowed, path, conditionScope );
                        AnchorConditionConfig condition;
                        condition.field = requireString( conditionEntry, "field", path, conditionScope );
                        require2D( condition.field, "condition" );
                        const auto opIt = conditionEntry.find( "op" );
                        if( opIt != conditionEntry.end() )
                        {
                            if( !opIt->is_string() )
                                configError( path, conditionScope + ".op must be a string" );
                            condition.condition.op = parseCompareOp( opIt->get<std::string>(), path,
                                                                     conditionScope );
                        }
                        readOptional( conditionEntry, "value", condition.condition.value, path, conditionScope );
                        readOptional( conditionEntry, "max", condition.condition.maxValue, path, conditionScope );
                        if( condition.condition.op == CompareOp::Between &&
                            condition.condition.maxValue < condition.condition.value )
                            configError( path, conditionScope + ".max must be >= value" );
                        anchor.conditions.push_back( std::move( condition ) );
                    }
                }
                cfg.anchorSets.push_back( std::move( anchor ) );
            }
        }

        std::unordered_set<std::string> decorationIds;
        if( const auto decorationIt = root.find( "decorations" ); decorationIt != root.end() )
        {
            if( !decorationIt->is_array() )
                configError( path, "field 'worldgen.decorations' must be an array" );
            std::size_t decorationIndex = 0;
            for( const json &entry : *decorationIt )
            {
                const std::string scope = "decorations[" + std::to_string( decorationIndex++ ) + "]";
                constexpr std::array allowed{
                    std::string_view( "id" ), std::string_view( "type" ),
                    std::string_view( "anchorSet" ), std::string_view( "priority" ),
                    std::string_view( "block" ), std::string_view( "minHeight" ),
                    std::string_view( "maxHeight" ), std::string_view( "script" ),
                    std::string_view( "function" ), std::string_view( "salt" ),
                    std::string_view( "bounds" ), std::string_view( "palette" ),
                    std::string_view( "anchorMin" ), std::string_view( "anchorMax" ),
                    std::string_view( "replaceBlocks" ), std::string_view( "replaceTags" ),
                    std::string_view( "supportBlocks" ), std::string_view( "supportTags" )
                };
                checkUnknownFields( entry, allowed, path, scope );

                DecorationPassConfig decoration;
                decoration.id = requireString( entry, "id", path, scope );
                if( !decorationIds.insert( decoration.id ).second )
                    configError( path, scope + ": duplicate decoration id '" + decoration.id + "'" );
                decoration.type = parseDecorationType( requireString( entry, "type", path, scope ),
                                                       path, scope );
                decoration.anchorSet = requireString( entry, "anchorSet", path, scope );
                if( anchorSetIds.count( decoration.anchorSet ) == 0 )
                    configError( path, scope + ": unknown anchorSet '" + decoration.anchorSet + "'" );
                readOptional( entry, "priority", decoration.priority, path, scope );
                readOptional( entry, "block", decoration.blockId, path, scope );
                readOptional( entry, "minHeight", decoration.minHeight, path, scope );
                readOptional( entry, "maxHeight", decoration.maxHeight, path, scope );
                readOptional( entry, "function", decoration.functionName, path, scope );
                readOptional( entry, "salt", decoration.salt, path, scope );
                readOptional( entry, "anchorMin", decoration.anchorMin, path, scope );
                readOptional( entry, "anchorMax", decoration.anchorMax, path, scope );
                decoration.palette = readStringArray( entry, "palette", path, scope );
                decoration.replaceBlocks = readStringArray( entry, "replaceBlocks", path, scope );
                decoration.replaceTags = readStringArray( entry, "replaceTags", path, scope );
                decoration.supportBlocks = readStringArray( entry, "supportBlocks", path, scope );
                decoration.supportTags = readStringArray( entry, "supportTags", path, scope );

                if( !std::isfinite( decoration.anchorMin ) || !std::isfinite( decoration.anchorMax ) ||
                    decoration.anchorMin < 0.0 || decoration.anchorMax > 1.0 ||
                    decoration.anchorMax <= decoration.anchorMin )
                    configError( path, scope + ": anchorMin/anchorMax must form a non-empty interval in 0..1" );

                if( decoration.type == DecorationType::Scatter ||
                    decoration.type == DecorationType::Column )
                {
                    if( decoration.blockId.empty() )
                        configError( path, scope + ": block is required for scatter/column" );
                    if( decoration.type == DecorationType::Column &&
                        ( decoration.minHeight < 1 || decoration.maxHeight < decoration.minHeight ) )
                        configError( path, scope + ": column heights must satisfy 1 <= minHeight <= maxHeight" );
                }
                else
                {
                    const std::string script = requireString( entry, "script", path, scope );
                    decoration.scriptPath = path.parent_path() / script;
                    if( decoration.functionName.empty() )
                        configError( path, scope + ": function must not be empty" );
                    if( decoration.palette.empty() )
                        configError( path, scope + ": structure palette must not be empty" );
                    const json *bounds = optionalObject( entry, "bounds", path, scope );
                    if( !bounds ) configError( path, scope + ": structure bounds are required" );
                    constexpr std::array boundsAllowed{
                        std::string_view( "minX" ), std::string_view( "maxX" ),
                        std::string_view( "minY" ), std::string_view( "maxY" ),
                        std::string_view( "minZ" ), std::string_view( "maxZ" )
                    };
                    checkUnknownFields( *bounds, boundsAllowed, path, scope + ".bounds" );
                    readOptional( *bounds, "minX", decoration.bounds.minX, path, scope + ".bounds" );
                    readOptional( *bounds, "maxX", decoration.bounds.maxX, path, scope + ".bounds" );
                    readOptional( *bounds, "minY", decoration.bounds.minY, path, scope + ".bounds" );
                    readOptional( *bounds, "maxY", decoration.bounds.maxY, path, scope + ".bounds" );
                    readOptional( *bounds, "minZ", decoration.bounds.minZ, path, scope + ".bounds" );
                    readOptional( *bounds, "maxZ", decoration.bounds.maxZ, path, scope + ".bounds" );
                    if( decoration.bounds.maxX < decoration.bounds.minX ||
                        decoration.bounds.maxY < decoration.bounds.minY ||
                        decoration.bounds.maxZ < decoration.bounds.minZ )
                        configError( path, scope + ": structure bounds min must be <= max" );
                }
                cfg.decorations.push_back( std::move( decoration ) );
            }
        }

        if( !cfg.decorations.empty() && cfg.anchorSets.empty() )
            configError( path, "decorations require at least one anchorSet" );

        return cfg;
    }
} // namespace worldgen
