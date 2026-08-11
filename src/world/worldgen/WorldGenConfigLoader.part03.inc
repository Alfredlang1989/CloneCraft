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
