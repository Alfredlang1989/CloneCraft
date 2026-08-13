                pass.replaceMask.assign( table.size(), 0u );
                if( pass.replaceBlockIds.empty() && pass.replaceTags.empty() )
                {
                    std::fill( pass.replaceMask.begin(), pass.replaceMask.end(), 1u );
                }
                else
                {
                    for( const std::uint16_t id : pass.replaceBlockIds )
                        if( id < pass.replaceMask.size() ) pass.replaceMask[id] = 1u;
                    for( std::size_t blockId = 0; blockId < blockTags.size(); ++blockId )
                    {
                        for( const std::string &tag : pass.replaceTags )
                        {
                            if( blockTags[blockId].count( tag ) != 0 )
                            {
                                pass.replaceMask[blockId] = 1u;
                                break;
                            }
                        }
                    }
                }
                if( pass.config.type == PassType::Volume &&
                    !pass.replaceMask.empty() && pass.replaceMask[0] != 0u )
                    volumeCanCreateInAir = true;
                passes.push_back( std::move( pass ) );
            }

            for( std::size_t i = 0; i < passes.size(); ++i )
                stageMergeOrders[passes[i].stageIndex].push_back( i );
            for( std::vector<std::size_t> &order : stageMergeOrders )
            {
                std::stable_sort( order.begin(), order.end(), [&]( std::size_t a, std::size_t b ) {
                    const RuntimePass &pa = passes[a];
                    const RuntimePass &pb = passes[b];
                    if( pa.config.priority != pb.config.priority )
                        return pa.config.priority < pb.config.priority;
                    return pa.order < pb.order;
                } );
            }

            const auto resolve2D = [&]( const std::string &id, const std::string &owner,
                                        const char *role ) -> std::size_t {
                const auto it = fieldById.find( id );
                if( it == fieldById.end() )
                    throw std::invalid_argument( owner + " references unknown " + role +
                                                 " field '" + id + "'" );
                if( fields[it->second].config().dimension != FieldDimension::D2 )
                    throw std::invalid_argument( owner + " " + role + " field '" + id +
                                                 "' must be 2D" );
                return it->second;
            };

            anchorSets.reserve( config.anchorSets.size() );
            for( const AnchorSetConfig &anchorConfig : config.anchorSets )
            {
                if( anchorSetById.count( anchorConfig.id ) != 0u )
                    throw std::invalid_argument( "duplicate decoration anchor set '" +
                                                 anchorConfig.id + "'" );
                RuntimeAnchorSet anchor;
                anchor.config = anchorConfig;
                anchor.surfaceField = resolve2D( anchorConfig.surfaceField,
                                                  "anchor set '" + anchorConfig.id + "'",
                                                  "surface" );
                if( !anchorConfig.densityField.empty() )
                    anchor.densityField = resolve2D( anchorConfig.densityField,
                                                      "anchor set '" + anchorConfig.id + "'",
                                                      "density" );
                if( !anchorConfig.biome.empty() )
                {
                    const auto biomeIt = biomeById.find( anchorConfig.biome );
                    if( biomeIt == biomeById.end() )
                        throw std::invalid_argument( "anchor set '" + anchorConfig.id +
                                                     "' references unknown biome '" +
                                                     anchorConfig.biome + "'" );
                    anchor.biomeIndex = biomeIt->second;
                }
                for( const AnchorConditionConfig &conditionConfig : anchorConfig.conditions )
                {
                    RuntimeAnchorCondition condition;
                    condition.field = resolve2D( conditionConfig.field,
                                                  "anchor set '" + anchorConfig.id + "'",
                                                  "condition" );
                    condition.condition = conditionConfig.condition;
                    anchor.conditions.push_back( condition );
                }
                anchorSetById.emplace( anchorConfig.id, anchorSets.size() );
                anchorSets.push_back( std::move( anchor ) );
            }

            const auto buildMask = [&]( const std::vector<std::string> &ids,
                                        const std::vector<std::string> &tags,
                                        bool defaultAll ) {
                std::vector<std::uint8_t> mask( table.size(), 0u );
                if( ids.empty() && tags.empty() && defaultAll )
                {
                    std::fill( mask.begin(), mask.end(), 1u );
                    return mask;
                }
                for( const std::string &id : ids )
                {
                    const std::uint16_t runtimeId = table.indexOf( id );
                    if( runtimeId < mask.size() ) mask[runtimeId] = 1u;
                }
                for( std::size_t blockId = 0; blockId < blockTags.size(); ++blockId )
                {
                    for( const std::string &tag : tags )
                    {
                        if( blockTags[blockId].count( tag ) != 0u )
                        {
                            mask[blockId] = 1u;
                            break;
                        }
                    }
                }
                return mask;
            };

            decorations.reserve( config.decorations.size() );
            for( std::size_t i = 0; i < config.decorations.size(); ++i )
            {
                RuntimeDecoration decoration;
                decoration.config = config.decorations[i];
                decoration.order = static_cast<std::uint32_t>( i );
                const auto anchorIt = anchorSetById.find( decoration.config.anchorSet );
                if( anchorIt == anchorSetById.end() )
                    throw std::invalid_argument( "decoration '" + decoration.config.id +
                                                 "' references unknown anchor set '" +
                                                 decoration.config.anchorSet + "'" );
                decoration.anchorSet = anchorIt->second;

                if( decoration.config.type == DecorationType::Structure )
                {
                    for( const std::string &id : decoration.config.palette )
                        decoration.paletteIds.push_back( table.indexOf( id ) );
                    FieldConfig scriptConfig;
                    scriptConfig.id = "decoration:" + decoration.config.id;
                    scriptConfig.dimension = FieldDimension::D3;
                    scriptConfig.scriptPath = decoration.config.scriptPath;
                    scriptConfig.functionName = decoration.config.functionName;
                    scriptConfig.salt = decoration.config.salt;
                    decoration.structureEvaluator =
                        std::make_unique<LuaFieldEvaluator>( scriptConfig, config.seed );
                }
                else
                {
                    decoration.blockId = table.indexOf( decoration.config.blockId );
                }

                decoration.replaceMask = buildMask( decoration.config.replaceBlocks,
                                                     decoration.config.replaceTags, true );
                decoration.supportMask = buildMask( decoration.config.supportBlocks,
                                                     decoration.config.supportTags, true );

                StructureBounds influence;
                if( decoration.config.type == DecorationType::Structure )
                    influence = decoration.config.bounds;
                else if( decoration.config.type == DecorationType::Column )
                    influence.maxY = decoration.config.maxHeight - 1;
                RuntimeAnchorSet &anchor = anchorSets[decoration.anchorSet];
                anchor.influenceBounds.minX = std::min( anchor.influenceBounds.minX, influence.minX );
                anchor.influenceBounds.maxX = std::max( anchor.influenceBounds.maxX, influence.maxX );
                anchor.influenceBounds.minY = std::min( anchor.influenceBounds.minY, influence.minY );
                anchor.influenceBounds.maxY = std::max( anchor.influenceBounds.maxY, influence.maxY );
                anchor.influenceBounds.minZ = std::min( anchor.influenceBounds.minZ, influence.minZ );
                anchor.influenceBounds.maxZ = std::max( anchor.influenceBounds.maxZ, influence.maxZ );
                decorations.push_back( std::move( decoration ) );
            }

            decorationMergeOrder.resize( decorations.size() );
            for( std::size_t i = 0; i < decorationMergeOrder.size(); ++i )
                decorationMergeOrder[i] = i;
            std::stable_sort( decorationMergeOrder.begin(), decorationMergeOrder.end(),
                              [&]( std::size_t a, std::size_t b ) {
                const RuntimeDecoration &da = decorations[a];
                const RuntimeDecoration &db = decorations[b];
                if( da.config.priority != db.config.priority )
                    return da.config.priority < db.config.priority;
                return da.order < db.order;
            } );
