#include "WorldGenConfigLoader.part00.inc"

#define loadWorldGenConfig loadBaseWorldGenConfig
#include "WorldGenConfigLoader.part01.inc"
#include "WorldGenConfigLoader.part02.inc"
#include "WorldGenConfigLoader.part03.inc"
#include "WorldGenConfigLoader.part04.inc"
#undef loadWorldGenConfig

#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace worldgen
{
    namespace
    {
        json loadOverlayRoot( const std::filesystem::path &path, const char *arrayName )
        {
            std::ifstream input( path );
            if( !input ) configError( path, "could not open overlay" );

            json root;
            try { input >> root; }
            catch( const json::parse_error &e )
            {
                configError( path, "JSON parse error at byte " + std::to_string( e.byte ) );
            }
            if( !root.is_object() || root.size() != 1u || !root.contains( arrayName ) ||
                !root[arrayName].is_array() )
                configError( path, std::string( "root must contain only a '" ) + arrayName + "' array" );
            return root;
        }

        void appendFieldOverlay( WorldGenConfig &cfg, const std::filesystem::path &path )
        {
            if( !std::filesystem::exists( path ) ) return;
            const json root = loadOverlayRoot( path, "fields" );

            std::unordered_set<std::string> fieldIds;
            for( const FieldConfig &field : cfg.fields ) fieldIds.insert( field.id );

            std::size_t index = 0;
            for( const json &entry : root["fields"] )
            {
                const std::string scope = "fields[" + std::to_string( index++ ) + "]";
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
                field.scriptPath = path.parent_path() / requireString( entry, "script", path, scope );
                readOptional( entry, "function", field.functionName, path, scope );
                readOptional( entry, "salt", field.salt, path, scope );
                if( field.functionName.empty() )
                    configError( path, scope + ": function must not be empty" );
                cfg.fields.push_back( std::move( field ) );
            }
        }

        void appendPassOverlay( WorldGenConfig &cfg, const std::filesystem::path &path )
        {
            if( !std::filesystem::exists( path ) ) return;
            const json root = loadOverlayRoot( path, "passes" );

            std::unordered_map<std::string, FieldDimension> fieldDimensions;
            for( const FieldConfig &field : cfg.fields ) fieldDimensions[field.id] = field.dimension;
            std::unordered_set<std::string> stageIds;
            for( const StageConfig &stage : cfg.stages ) stageIds.insert( stage.id );
            std::unordered_set<std::string> passIds;
            for( const PassConfig &pass : cfg.passes ) passIds.insert( pass.id );

            std::size_t index = 0;
            for( const json &entry : root["passes"] )
            {
                const std::string scope = "passes[" + std::to_string( index++ ) + "]";
                constexpr std::array allowed{
                    std::string_view( "id" ), std::string_view( "type" ),
                    std::string_view( "stage" ), std::string_view( "block" ),
                    std::string_view( "priority" ), std::string_view( "field" ),
                    std::string_view( "maskField" ), std::string_view( "surfaceField" ),
                    std::string_view( "thicknessField" ), std::string_view( "bottomField" ),
                    std::string_view( "thickness" ), std::string_view( "surfaceOffset" ),
                    std::string_view( "bottomOffset" ), std::string_view( "condition" ),
                    std::string_view( "maskCondition" ), std::string_view( "replaceBlocks" ),
                    std::string_view( "replaceTags" )
                };
                checkUnknownFields( entry, allowed, path, scope );

                PassConfig pass;
                pass.id = requireString( entry, "id", path, scope );
                if( !passIds.insert( pass.id ).second )
                    configError( path, scope + ": duplicate pass id '" + pass.id + "'" );
                pass.type = parsePassType( requireString( entry, "type", path, scope ), path, scope );
                pass.stage = requireString( entry, "stage", path, scope );
                if( stageIds.count( pass.stage ) == 0u )
                    configError( path, scope + ": unknown stage '" + pass.stage + "'" );
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
                    if( const json *condition = optionalObject( entry, name, path, scope ) )
                    {
                        constexpr std::array conditionAllowed{
                            std::string_view( "op" ), std::string_view( "value" ),
                            std::string_view( "max" )
                        };
                        const std::string conditionScope = scope + "." + name;
                        checkUnknownFields( *condition, conditionAllowed, path, conditionScope );
                        if( const auto opIt = condition->find( "op" ); opIt != condition->end() )
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

                const auto requireField = [&]( const std::string &id, const char *role,
                                               FieldDimension expected ) {
                    if( id.empty() ) configError( path, scope + ": missing required " + role );
                    const auto it = fieldDimensions.find( id );
                    if( it == fieldDimensions.end() )
                        configError( path, scope + ": unknown field '" + id + "'" );
                    if( it->second != expected )
                        configError( path, scope + ": " + role + " field '" + id +
                                           "' has wrong dimension" );
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
        }
    } // namespace

    WorldGenConfig loadWorldGenConfig( const std::filesystem::path &path )
    {
        WorldGenConfig cfg = loadBaseWorldGenConfig( path );
        const std::filesystem::path dataDir = path.parent_path();
        appendFieldOverlay( cfg, dataDir / "worldgen_fields.json" );
        appendPassOverlay( cfg, dataDir / "worldgen_passes.json" );
        return cfg;
    }
} // namespace worldgen
