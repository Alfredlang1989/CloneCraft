            const auto stagesIt = root.find( "stages" );
            if( stagesIt == root.end() || !stagesIt->is_array() )
                configError( path, "field 'stageRegistry.stages' must be an array" );

            std::vector<StageConfig> stages;
            std::unordered_set<std::string> ids;
            std::size_t index = 0;
            for( const json &entry : *stagesIt )
            {
                const std::string scope = "stages[" + std::to_string( index++ ) + "]";
                constexpr std::array allowed{ std::string_view( "id" ),
                                              std::string_view( "order" ) };
                checkUnknownFields( entry, allowed, path, scope );

                StageConfig stage;
                stage.id = requireString( entry, "id", path, scope );
                if( !ids.insert( stage.id ).second )
                    configError( path, scope + ": duplicate stage id '" + stage.id + "'" );
                const auto orderIt = entry.find( "order" );
                if( orderIt == entry.end() || !orderIt->is_number_integer() )
                    configError( path, "field '" + scope + ".order' must be an integer" );
                try
                {
                    stage.order = orderIt->get<std::int32_t>();
                }
                catch( const json::exception & )
                {
                    configError( path, "field '" + scope + ".order' is outside int32 range" );
                }
                stages.push_back( std::move( stage ) );
            }
            if( stages.empty() )
                configError( path, "stageRegistry.stages must contain at least one stage" );
            return stages;
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
            std::string_view( "surfaceField" ), std::string_view( "stageRegistry" ),
            std::string_view( "fields" ),
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

        std::string stageRegistry = "stage.json";
        readOptional( root, "stageRegistry", stageRegistry, path, "worldgen" );
        if( stageRegistry.empty() )
            configError( path, "field 'worldgen.stageRegistry' must not be empty" );
        cfg.stageRegistryPath = path.parent_path() / stageRegistry;
        cfg.stages = loadStageRegistry( cfg.stageRegistryPath );
        std::unordered_set<std::string> stageIds;
        for( const StageConfig &stage : cfg.stages )
            stageIds.insert( stage.id );

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
