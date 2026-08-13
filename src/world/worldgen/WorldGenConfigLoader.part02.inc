                std::string_view( "surfaceField" ), std::string_view( "thicknessField" ),
                std::string_view( "bottomField" ), std::string_view( "thickness" ),
                std::string_view( "surfaceOffset" ), std::string_view( "bottomOffset" ),
                std::string_view( "condition" ), std::string_view( "maskCondition" ),
                std::string_view( "biomeCondition" ),
                std::string_view( "replaceBlocks" ),
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
                configError( path, scope + ": unknown stage '" + pass.stage +
                                   "' (declare it in " + cfg.stageRegistryPath.string() + ")" );
            pass.blockId = requireString( entry, "block", path, scope );
            readOptional( entry, "priority", pass.priority, path, scope );
            readOptional( entry, "field", pass.field, path, scope );
            readOptional( entry, "maskField", pass.maskField, path, scope );
            readOptional( entry, "biome", pass.biome, path, scope );
            readOptional( entry, "biomeDominant", pass.biomeDominant, path, scope );
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
            parseCondition( "biomeCondition", pass.biomeCondition );

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
        // reach tests; decoration proposals are applied only after every stage
        // declared by the external stage registry has completed.
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
                    std::string_view( "biome" ), std::string_view( "biomeCondition" ),
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
                readOptional( entry, "biome", anchor.biome, path, scope );
                readOptional( entry, "surfaceOffset", anchor.surfaceOffset, path, scope );
                readOptional( entry, "spacing", anchor.spacing, path, scope );
                readOptional( entry, "maxSurfaceDrop", anchor.maxSurfaceDrop, path, scope );
                readOptional( entry, "chance", anchor.chance, path, scope );
                readOptional( entry, "salt", anchor.salt, path, scope );
                if( const auto biomeCondition = optionalObject( entry, "biomeCondition", path, scope ) )
                {
                    constexpr std::array conditionAllowed{
                        std::string_view( "op" ), std::string_view( "value" ),
                        std::string_view( "max" )
                    };
                    const std::string conditionScope = scope + ".biomeCondition";
                    checkUnknownFields( *biomeCondition, conditionAllowed, path, conditionScope );
                    const auto opIt = biomeCondition->find( "op" );
                    if( opIt != biomeCondition->end() )
                    {
                        if( !opIt->is_string() )
                            configError( path, conditionScope + ".op must be a string" );
                        anchor.biomeCondition.op = parseCompareOp( opIt->get<std::string>(), path, conditionScope );
                    }
                    readOptional( *biomeCondition, "value", anchor.biomeCondition.value, path, conditionScope );
                    readOptional( *biomeCondition, "max", anchor.biomeCondition.maxValue, path, conditionScope );
                    if( anchor.biomeCondition.op == CompareOp::Between &&
                        anchor.biomeCondition.maxValue < anchor.biomeCondition.value )
                        configError( path, conditionScope + ".max must be >= value" );
                }
                if( anchor.spacing < 1 )
                    configError( path, scope + ": spacing must be >= 1" );
                if( anchor.maxSurfaceDrop < 0 )
                    configError( path, scope + ": maxSurfaceDrop must be >= 0" );
                if( !std::isfinite( anchor.chance ) || anchor.chance < 0.0 || anchor.chance > 1.0 )
                    configError( path, scope + ": chance must be in 0..1" );

                auto require2D = [&]( const std::string &id, const char *role ) {
                    if( id.empty() )
