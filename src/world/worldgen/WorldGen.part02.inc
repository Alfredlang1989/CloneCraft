                           std::size_t &index ) noexcept
        {
            world::RelativeI64 d{};
            if( !world::blockDeltaWithin( point, origin, EDGE - 1, d ) ||
                d.x < 0 || d.y < 0 || d.z < 0 || d.x >= EDGE || d.y >= EDGE || d.z >= EDGE )
                return false;
            index = denseIndex( d.x, d.y, d.z );
            return true;
        }
    } // namespace
    struct WorldGen::State
    {
        State( const WorldGenConfig &c,
               const world::BlockRegistry &blocks,
               const world::BlockIdTable &idTable,
               const world::BiomeRegistry *biomes )
            : config( c ), table( idTable )
        {
            if( config.fields.empty() )
                throw std::invalid_argument( "WorldGen requires at least one configured field" );
            if( config.passes.empty() )
                throw std::invalid_argument( "WorldGen requires at least one configured pass" );

            fields.reserve( config.fields.size() );
            for( const FieldConfig &field : config.fields )
            {
                if( fieldById.count( field.id ) != 0 )
                    throw std::invalid_argument( "duplicate worldgen field '" + field.id + "'" );
                const std::size_t fieldIndex = fields.size();
                fieldById[field.id] = fieldIndex;
                fields.emplace_back( field, config.seed );
                if( field.dimension == FieldDimension::D2 )
                    field2DIndices.push_back( fieldIndex );
                else
                    field3DIndices.push_back( fieldIndex );
            }

            const auto surfaceIt = fieldById.find( config.surfaceField );
            if( surfaceIt == fieldById.end() )
                throw std::invalid_argument( "unknown worldgen surfaceField '" +
                                             config.surfaceField + "'" );
            surfaceField = surfaceIt->second;
            if( fields[surfaceField].config().dimension != FieldDimension::D2 )
                throw std::invalid_argument( "worldgen surfaceField must be 2D" );

            if( biomes && !biomes->empty() )
            {
                for( const std::string &biomeId : biomes->ids() )
                {
                    const world::BiomeDef &biome = biomes->get( biomeId );
                    RuntimeBiomeTerrain runtime;
                    runtime.id = biome.id;
                    runtime.terrain = biome.terrain;
                    runtime.weight = biome.weight;
                    runtime.selectionSharpness = biome.selectionSharpness;
                    runtime.salt = stableStringSalt( biome.id );

                    for( const world::BiomeSelectionFieldDef &rule : biome.selectionFields )
                    {
                        const auto fieldIt = fieldById.find( rule.field );
                        if( fieldIt == fieldById.end() )
                            throw std::invalid_argument( "biome '" + biome.id +
                                "' selection references unknown field '" + rule.field + "'" );
                        if( fields[fieldIt->second].config().dimension != FieldDimension::D2 )
                            throw std::invalid_argument( "biome '" + biome.id +
                                "' selection field '" + rule.field + "' must be 2D" );
                        runtime.selectionFields.push_back( { fieldIt->second, rule.minValue,
                                                             rule.maxValue, rule.fade } );
                    }

                    if( biome.terrainMaskField.empty() )
                    {
                        if( fallbackBiomeIndex != NO_FIELD )
                            throw std::invalid_argument(
                                "multiple biomes without terrainMaskField; exactly one fallback terrain is allowed" );
                        fallbackBiomeIndex = biomeTerrains.size();
                    }
                    else
                    {
                        const auto maskIt = fieldById.find( biome.terrainMaskField );
                        if( maskIt == fieldById.end() )
                            throw std::invalid_argument( "biome '" + biome.id +
                                                         "' references unknown terrainMaskField '" +
                                                         biome.terrainMaskField + "'" );
                        if( fields[maskIt->second].config().dimension != FieldDimension::D2 )
                            throw std::invalid_argument( "biome '" + biome.id +
                                                         "' terrainMaskField must be 2D" );
                        runtime.maskField = maskIt->second;
                    }

                    const std::size_t runtimeIndex = biomeTerrains.size();
                    if( !biomeById.emplace( biome.id, runtimeIndex ).second )
                        throw std::invalid_argument( "duplicate runtime biome '" + biome.id + "'" );
                    biomeTerrains.push_back( std::move( runtime ) );
                }

                if( fallbackBiomeIndex == NO_FIELD )
                {
                    RuntimeBiomeTerrain fallback;
                    fallback.id = "<implicit-default>";
                    fallback.terrain = world::BiomeTerrainDef{};
                    fallback.salt = stableStringSalt( fallback.id );
                    fallbackBiomeIndex = biomeTerrains.size();
                    biomeById.emplace( fallback.id, fallbackBiomeIndex );
                    biomeTerrains.push_back( std::move( fallback ) );
                }
                legacyBiomeMaskBlend = std::all_of( biomeTerrains.begin(), biomeTerrains.end(),
                    []( const RuntimeBiomeTerrain &biome ) { return biome.selectionFields.empty(); } );
            }

            blockTags.resize( table.size() );
            for( std::size_t i = 0; i < table.size(); ++i )
            {
                const world::BlockDef &def = blocks.get( table.idOf( static_cast<std::uint16_t>( i ) ) );
                blockTags[i].insert( def.tags.begin(), def.tags.end() );
            }

            if( config.stages.empty() )
                throw std::invalid_argument( "WorldGen requires at least one configured stage" );

            std::vector<std::size_t> configuredStageOrder( config.stages.size() );
            for( std::size_t i = 0; i < configuredStageOrder.size(); ++i )
                configuredStageOrder[i] = i;
            std::stable_sort( configuredStageOrder.begin(), configuredStageOrder.end(),
                              [&]( std::size_t a, std::size_t b ) {
                return config.stages[a].order < config.stages[b].order;
            } );

            stageMergeOrders.resize( config.stages.size() );
            for( std::size_t runtimeIndex = 0; runtimeIndex < configuredStageOrder.size(); ++runtimeIndex )
            {
                const StageConfig &stage = config.stages[configuredStageOrder[runtimeIndex]];
                if( stage.id.empty() )
                    throw std::invalid_argument( "worldgen stage id must not be empty" );
                if( stageById.count( stage.id ) != 0u )
                    throw std::invalid_argument( "duplicate worldgen stage '" + stage.id + "'" );
                stageById.emplace( stage.id, runtimeIndex );
            }

            passes.reserve( config.passes.size() );
            for( std::size_t i = 0; i < config.passes.size(); ++i )
            {
                RuntimePass pass;
                pass.config = config.passes[i];
                pass.order = static_cast<std::uint32_t>( i );
                const auto stageIt = stageById.find( pass.config.stage );
                if( stageIt == stageById.end() )
                    throw std::invalid_argument( "pass '" + pass.config.id +
                                                 "' references unknown stage '" +
                                                 pass.config.stage + "'" );
                pass.stageIndex = stageIt->second;
                pass.blockId = table.indexOf( pass.config.blockId );
                for( const std::string &id : pass.config.replaceBlocks )
                    pass.replaceBlockIds.push_back( table.indexOf( id ) );
                pass.replaceTags.insert( pass.config.replaceTags.begin(),
                                         pass.config.replaceTags.end() );

                auto resolve = [&]( const std::string &id, FieldDimension expected,
                                    const char *role ) -> std::size_t {
                    const auto it = fieldById.find( id );
                    if( it == fieldById.end() )
                        throw std::invalid_argument( "pass '" + pass.config.id +
                                                     "' references unknown " + role +
                                                     " field '" + id + "'" );
                    if( fields[it->second].config().dimension != expected )
                        throw std::invalid_argument( "pass '" + pass.config.id + "' " + role +
                                                     " field '" + id + "' has wrong dimension" );
                    return it->second;
                };

                if( !pass.config.maskField.empty() )
                    pass.maskField = resolve( pass.config.maskField, FieldDimension::D2, "mask" );
                if( !pass.config.biome.empty() )
                {
                    const auto biomeIt = biomeById.find( pass.config.biome );
                    if( biomeIt == biomeById.end() )
                        throw std::invalid_argument( "pass '" + pass.config.id +
                                                     "' references unknown biome '" +
                                                     pass.config.biome + "'" );
                    pass.biomeIndex = biomeIt->second;
                }

                switch( pass.config.type )
                {
                case PassType::FillBelow:
                case PassType::Surface:
                    pass.field = resolve( pass.config.field, FieldDimension::D2, "source" );
                    break;
                case PassType::Volume:
                    pass.field = resolve( pass.config.field, FieldDimension::D3, "source" );
                    break;
                case PassType::SurfaceLayer:
                    pass.surfaceField = resolve( pass.config.surfaceField,
                                                 FieldDimension::D2, "surface" );
                    if( !pass.config.thicknessField.empty() )
                        pass.thicknessField = resolve( pass.config.thicknessField,
                                                       FieldDimension::D2, "thickness" );
                    if( !pass.config.bottomField.empty() )
                        pass.bottomField = resolve( pass.config.bottomField,
                                                    FieldDimension::D2, "bottom" );
                    if( pass.bottomField == NO_FIELD && pass.thicknessField == NO_FIELD &&
                        pass.config.thickness < 1 )
                        throw std::invalid_argument( "pass '" + pass.config.id +
                                                     "' thickness must be >= 1" );
                    break;
                }
