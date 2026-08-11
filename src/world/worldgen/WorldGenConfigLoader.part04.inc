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
