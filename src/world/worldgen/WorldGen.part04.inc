
            const std::size_t maxJobs = std::max( { field2DIndices.size(), field3DIndices.size(),
                                                   passes.size(), anchorSets.size(), decorations.size(),
                                                   std::size_t( 1 ) } );
            executor = std::make_unique<BatchExecutor>(
                resolvedWorkerCount( config.workerThreads, maxJobs ) );
        }

        WorldGenConfig config;
        world::BlockIdTable table;
        std::vector<LuaFieldEvaluator> fields;
        std::unordered_map<std::string, std::size_t> fieldById;
        std::vector<std::size_t> field2DIndices, field3DIndices;
        std::size_t surfaceField = NO_FIELD;
        std::vector<RuntimeBiomeTerrain> biomeTerrains;
        std::unordered_map<std::string, std::size_t> biomeById;
        std::size_t fallbackBiomeIndex = NO_FIELD;
        bool legacyBiomeMaskBlend = false;
        std::vector<RuntimePass> passes;
        std::unordered_map<std::string, std::size_t> stageById;
        std::vector<std::vector<std::size_t>> stageMergeOrders;
        std::vector<std::unordered_set<std::string>> blockTags;
        bool volumeCanCreateInAir = false;
        std::vector<RuntimeAnchorSet> anchorSets;
        std::unordered_map<std::string, std::size_t> anchorSetById;
        std::vector<RuntimeDecoration> decorations;
        std::vector<std::size_t> decorationMergeOrder;
        std::unique_ptr<BatchExecutor> executor;

        double terrainProfileHeight( const RuntimeBiomeTerrain &biome, double rawBase,
                                     MappedOpenSimplexNoise &noise ) const
        {
            const world::BiomeTerrainDef &terrain = biome.terrain;
            double height = terrain.heightOffset + rawBase * terrain.heightMultiplier;

            if( terrain.detailMultiplier > 0.0 )
            {
                const double detail = noise.noise2( terrain.detailScale,
                                                    biome.salt ^ UINT64_C( 0xD37A11A5 ) );
                height += detail * terrain.detailAmplitude * terrain.detailMultiplier;
            }

            if( terrain.ridgeAmplitude > 0.0 )
            {
                const double n = noise.noise2( terrain.ridgeScale,
                                                biome.salt ^ UINT64_C( 0xA17E9D63 ) );
                const double ridge = std::pow( std::clamp( 1.0 - std::abs( n ), 0.0, 1.0 ),
                                               terrain.ridgeSharpness );
                height += ridge * terrain.ridgeAmplitude;
            }

            if( terrain.islandAmplitude > 0.0 )
            {
                const double n = 0.5 + 0.5 * noise.noise2(
                    terrain.islandScale, biome.salt ^ UINT64_C( 0x15A1A6D5 ) );
                if( n > terrain.islandThreshold )
                {
                    const double denom = std::max( 1.0e-9, 1.0 - terrain.islandThreshold );
                    const double island = std::pow(
                        std::clamp( ( n - terrain.islandThreshold ) / denom, 0.0, 1.0 ),
                        terrain.islandSharpness );
                    height += island * terrain.islandAmplitude;
                }
            }
            return height;
        }

        static double smoothUnit( double t )
        {
            t = std::clamp( t, 0.0, 1.0 );
            return t * t * ( 3.0 - 2.0 * t );
        }

        static double selectionMembership( double value, const RuntimeBiomeSelectionField &rule )
        {
            if( value >= rule.minValue && value <= rule.maxValue ) return 1.0;
            if( rule.fade <= 0.0 ) return 0.0;
            if( value < rule.minValue )
                return smoothUnit( ( value - ( rule.minValue - rule.fade ) ) / rule.fade );
            return 1.0 - smoothUnit( ( value - rule.maxValue ) / rule.fade );
        }

        template <typename FieldSampler>
        void calculateBiomeWeights( FieldSampler &&sampleField, std::vector<double> &out ) const
        {
            out.assign( biomeTerrains.size(), 0.0 );
            if( biomeTerrains.empty() ) return;

            if( legacyBiomeMaskBlend )
            {
                double explicitSum = 0.0;
                for( std::size_t i = 0; i < biomeTerrains.size(); ++i )
                {
                    if( i == fallbackBiomeIndex ) continue;
                    const RuntimeBiomeTerrain &biome = biomeTerrains[i];
                    if( biome.maskField == NO_FIELD ) continue;
                    out[i] = std::clamp( sampleField( biome.maskField ), 0.0, 1.0 );
                    explicitSum += out[i];
                }
                if( fallbackBiomeIndex != NO_FIELD && fallbackBiomeIndex < out.size() )
                    out[fallbackBiomeIndex] = std::max( 0.0, 1.0 - explicitSum );
                const double totalLegacy = explicitSum +
                    ( fallbackBiomeIndex < out.size() ? out[fallbackBiomeIndex] : 0.0 );
                if( totalLegacy > 1.0e-12 )
                    for( double &weight : out ) weight /= totalLegacy;
                return;
            }

            double total = 0.0;
            for( std::size_t i = 0; i < biomeTerrains.size(); ++i )
            {
                const RuntimeBiomeTerrain &biome = biomeTerrains[i];
                double regional = 1.0;
                if( biome.maskField != NO_FIELD )
                    regional = std::clamp( sampleField( biome.maskField ), 0.0, 1.0 );
                if( regional <= 0.0 ) continue;

                double suitability = 1.0;
                if( !biome.selectionFields.empty() )
                {
                    double product = 1.0;
                    for( const RuntimeBiomeSelectionField &rule : biome.selectionFields )
                    {
                        const double member = selectionMembership( sampleField( rule.field ), rule );
                        if( member <= 0.0 )
                        {
                            product = 0.0;
                            break;
                        }
                        product *= member;
                    }
                    suitability = product > 0.0
                        ? std::pow( product, 1.0 / static_cast<double>( biome.selectionFields.size() ) )
                        : 0.0;
                }
                if( suitability <= 0.0 ) continue;

                const double score = regional *
                    std::pow( suitability, biome.selectionSharpness ) * biome.weight;
                out[i] = score;
                total += score;
            }

            if( total <= 1.0e-12 )
            {
                if( fallbackBiomeIndex != NO_FIELD && fallbackBiomeIndex < out.size() )
                    out[fallbackBiomeIndex] = 1.0;
                return;
            }
            for( double &weight : out ) weight /= total;
        }

        double blendedSurfaceHeight( double rawBase, const world::BlockAddress &column,
                                     MappedOpenSimplexNoise &noise,
                                     const std::vector<double> &weights ) const
        {
            if( weights.empty() ) return rawBase;
            noise.setSample( column );
            double height = 0.0;
            double total = 0.0;
            for( std::size_t i = 0; i < weights.size(); ++i )
            {
                if( weights[i] <= 0.0 ) continue;
                height += weights[i] * terrainProfileHeight( biomeTerrains[i], rawBase, noise );
                total += weights[i];
            }
            return total > 0.0 ? height / total : rawBase;
        }

        std::vector<double> biomeWeightsAt( const world::BlockAddress &column ) const
        {
            std::vector<double> weights;
            calculateBiomeWeights( [&]( std::size_t fieldIndex ) {
                return fields[fieldIndex].sample2D( column );
            }, weights );
            return weights;
        }

        double adjustedSurfaceAt( const world::BlockAddress &column ) const
        {
            const double rawBase = fields[surfaceField].sample2D( column );
            if( biomeTerrains.empty() ) return rawBase;
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            return blendedSurfaceHeight( rawBase, column, noise, biomeWeightsAt( column ) );
        }

        using BiomeWeightGrid = std::vector<double>;

        std::size_t biomeGridIndex( std::size_t biomeIndex, std::int64_t lx, std::int64_t lz ) const
        {
            return ( biomeIndex * static_cast<std::size_t>( EDGE ) + static_cast<std::size_t>( lx ) ) *
                   static_cast<std::size_t>( EDGE ) + static_cast<std::size_t>( lz );
        }

        void applyBiomeTerrain( const world::ChunkAddress &chunk,
                                std::vector<SampledField> &sampled,
                                BiomeWeightGrid &biomeWeights ) const
        {
            if( biomeTerrains.empty() ) return;
            biomeWeights.assign( biomeTerrains.size() * static_cast<std::size_t>( EDGE ) *
                                 static_cast<std::size_t>( EDGE ), 0.0 );
            SampledField &surface = sampled[surfaceField];
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            std::vector<double> weights;
            for( std::int64_t lx = 0; lx < EDGE; ++lx )
                for( std::int64_t lz = 0; lz < EDGE; ++lz )
                {
                    const world::BlockAddress column = world::blockAt( chunk, { lx, 0, lz } );
                    calculateBiomeWeights( [&]( std::size_t fieldIndex ) {
                        return sampled[fieldIndex].at2D( lx, lz );
                    }, weights );
                    for( std::size_t i = 0; i < weights.size(); ++i )
                        biomeWeights[biomeGridIndex( i, lx, lz )] = weights[i];

                    const std::size_t index = static_cast<std::size_t>( lx * EDGE + lz );
                    surface.values[index] = blendedSurfaceHeight( surface.values[index], column, noise, weights );
                }
        }

        bool canReplace( std::uint16_t current, const RuntimePass &pass ) const
        {
            return current < pass.replaceMask.size() && pass.replaceMask[current] != 0u;
        }

        bool columnEnabled( const RuntimePass &pass, const std::vector<SampledField> &sampled,
                            const BiomeWeightGrid &biomeWeights,
                            std::int64_t lx, std::int64_t lz ) const
        {
            if( pass.maskField != NO_FIELD &&
                !matchesCondition( sampled[pass.maskField].at2D( lx, lz ), pass.config.maskCondition ) )
                return false;
            if( pass.biomeIndex != NO_FIELD )
            {
                if( biomeWeights.empty() ) return false;
                const double weight = biomeWeights[biomeGridIndex( pass.biomeIndex, lx, lz )];
                if( !matchesCondition( weight, pass.config.biomeCondition ) ) return false;
                if( pass.config.biomeDominant )
                {
                    for( std::size_t i = 0; i < biomeTerrains.size(); ++i )
                        if( biomeWeights[biomeGridIndex( i, lx, lz )] > weight ) return false;
                }
            }
            return true;
        }

        std::int64_t shiftedSurface( double raw, const std::string &fieldId,
                                     std::int32_t offset ) const
        {
            const std::int64_t base = floorToBlock( raw, fieldId );
            if( ( offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset ) ||
                ( offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset ) )
                throw std::overflow_error( "worldgen surface scalar offset exceeds int64" );
            return base + static_cast<std::int64_t>( offset );
        }

        struct TerrainSurfaceSample
        {
            world::BlockAddress position{};
            std::uint16_t blockId = 0;
        };

        std::optional<TerrainSurfaceSample> resolvePostprocessSurface(
            const RuntimeAnchorSet &anchorSet, const world::BlockAddress &column ) const
        {
            std::vector<double> cache2D( fields.size(), 0.0 );
            std::vector<std::uint8_t> cached2D( fields.size(), 0u );
            const auto sample2D = [&]( std::size_t index ) -> double {
                if( cached2D[index] == 0u )
                {
                    cache2D[index] = index == surfaceField
                        ? adjustedSurfaceAt( column )
                        : fields[index].sample2D( column );
                    cached2D[index] = 1u;
                }
                return cache2D[index];
            };
            const std::vector<double> resolvedBiomeWeights = biomeWeightsAt( column );
            const auto columnEnabledAt = [&]( const RuntimePass &pass ) {
                if( pass.maskField != NO_FIELD &&
                    !matchesCondition( sample2D( pass.maskField ), pass.config.maskCondition ) )
                    return false;
                if( pass.biomeIndex != NO_FIELD )
                {
                    if( pass.biomeIndex >= resolvedBiomeWeights.size() ||
                        !matchesCondition( resolvedBiomeWeights[pass.biomeIndex],
                                           pass.config.biomeCondition ) )
                        return false;
                    if( pass.config.biomeDominant )
                    {
                        const double weight = resolvedBiomeWeights[pass.biomeIndex];
                        for( const double candidate : resolvedBiomeWeights )
                            if( candidate > weight ) return false;
                    }
                }
                return true;
            };
            const auto appliesAt = [&]( const RuntimePass &pass, const world::BlockAddress &point ) {
