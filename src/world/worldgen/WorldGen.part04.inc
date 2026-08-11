
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
        std::optional<RuntimeBiomeTerrain> fallbackTerrain;
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

        template <typename MaskSampler>
        double adjustedSurfaceHeight( double rawBase, const world::BlockAddress &column,
                                      MappedOpenSimplexNoise &noise,
                                      MaskSampler &&sampleMask ) const
        {
            if( !fallbackTerrain.has_value() ) return rawBase;

            noise.setSample( column );
            double weighted = 0.0;
            double explicitWeight = 0.0;
            for( const RuntimeBiomeTerrain &biome : biomeTerrains )
            {
                const double weight = std::clamp( sampleMask( biome.maskField ), 0.0, 1.0 );
                if( weight <= 0.0 ) continue;
                weighted += weight * terrainProfileHeight( biome, rawBase, noise );
                explicitWeight += weight;
            }

            const double fallbackWeight = std::max( 0.0, 1.0 - explicitWeight );
            if( fallbackWeight > 0.0 )
                weighted += fallbackWeight * terrainProfileHeight(
                    *fallbackTerrain, rawBase, noise );

            const double totalWeight = explicitWeight + fallbackWeight;
            return totalWeight > 0.0 ? weighted / totalWeight : rawBase;
        }

        double adjustedSurfaceAt( const world::BlockAddress &column ) const
        {
            const double rawBase = fields[surfaceField].sample2D( column );
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            return adjustedSurfaceHeight( rawBase, column, noise, [&]( std::size_t maskField ) {
                return fields[maskField].sample2D( column );
            } );
        }

        void applyBiomeTerrain( const world::ChunkAddress &chunk,
                                std::vector<SampledField> &sampled ) const
        {
            if( !fallbackTerrain.has_value() ) return;
            SampledField &surface = sampled[surfaceField];
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            for( std::int64_t lx = 0; lx < EDGE; ++lx )
                for( std::int64_t lz = 0; lz < EDGE; ++lz )
                {
                    const world::BlockAddress column = world::blockAt( chunk, { lx, 0, lz } );
                    const std::size_t index = static_cast<std::size_t>( lx * EDGE + lz );
                    const double rawBase = surface.values[index];
                    surface.values[index] = adjustedSurfaceHeight(
                        rawBase, column, noise, [&]( std::size_t maskField ) {
                            return sampled[maskField].at2D( lx, lz );
                        } );
                }
        }

        bool canReplace( std::uint16_t current, const RuntimePass &pass ) const
        {
            return current < pass.replaceMask.size() && pass.replaceMask[current] != 0u;
        }

        bool columnEnabled( const RuntimePass &pass, const std::vector<SampledField> &sampled,
                            std::int64_t lx, std::int64_t lz ) const
        {
            return pass.maskField == NO_FIELD ||
                   matchesCondition( sampled[pass.maskField].at2D( lx, lz ), pass.config.maskCondition );
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
            const auto columnEnabledAt = [&]( const RuntimePass &pass ) {
                return pass.maskField == NO_FIELD ||
                    matchesCondition( sample2D( pass.maskField ), pass.config.maskCondition );
            };
            const auto appliesAt = [&]( const RuntimePass &pass, const world::BlockAddress &point ) {
