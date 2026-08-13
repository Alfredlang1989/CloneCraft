                            }
                        }
                    }
                    if( applies ) emit(lx,ly,lz);
                }
            }
            return proposals;
        }

        double fieldValueAt( std::size_t fieldIndex, const world::BlockAddress &point,
                             const world::BlockAddress &chunkOrigin,
                             const std::vector<SampledField> &sampled ) const
        {
            world::RelativeI64 d{};
            if( world::blockDeltaWithin( point, chunkOrigin, EDGE-1, d ) &&
                d.x>=0 && d.x<EDGE && d.z>=0 && d.z<EDGE &&
                fieldIndex < sampled.size() && !sampled[fieldIndex].values.empty() )
                return sampled[fieldIndex].at2D( d.x, d.z );
            return fieldIndex == surfaceField ? adjustedSurfaceAt( point )
                                              : fields[fieldIndex].sample2D( point );
        }

        double biomeWeightAtColumn( std::size_t biomeIndex, const world::BlockAddress &column,
                                    const world::BlockAddress &origin,
                                    const BiomeWeightGrid &biomeWeights ) const
        {
            world::RelativeI64 d{};
            if( !biomeWeights.empty() && world::blockDeltaWithin( column, origin, EDGE - 1, d ) &&
                d.x >= 0 && d.x < EDGE && d.z >= 0 && d.z < EDGE )
                return biomeWeights[biomeGridIndex( biomeIndex, d.x, d.z )];
            const std::vector<double> weights = biomeWeightsAt( column );
            return biomeIndex < weights.size() ? weights[biomeIndex] : 0.0;
        }

        std::vector<DecorationAnchor> buildAnchors( const RuntimeAnchorSet &anchorSet,
            const world::BlockAddress &origin, const std::vector<SampledField> &sampled,
            const BiomeWeightGrid &biomeWeights ) const
        {
            std::vector<DecorationAnchor> anchors;
            const std::int64_t minDx = -static_cast<std::int64_t>( anchorSet.influenceBounds.maxX );
            const std::int64_t maxDx = EDGE - 1 - static_cast<std::int64_t>( anchorSet.influenceBounds.minX );
            const std::int64_t minDz = -static_cast<std::int64_t>( anchorSet.influenceBounds.maxZ );
            const std::int64_t maxDz = EDGE - 1 - static_cast<std::int64_t>( anchorSet.influenceBounds.minZ );
            const std::int64_t spacing = anchorSet.config.spacing;

            for( std::int64_t dx=minDx; dx<=maxDx; ++dx ) for( std::int64_t dz=minDz; dz<=maxDz; ++dz )
            {
                world::BlockAddress column;
                if( !world::tryOffsetBlock( origin, dx, 0, dz, column ) ) continue;
                std::int64_t remX=0, remZ=0;
                const auto cellX = divideAxis( world::blockAxisX(column), spacing, remX );
                const auto cellZ = divideAxis( world::blockAxisZ(column), spacing, remZ );
                const std::uint64_t h0 = hashAnchorCell( config.seed, anchorSet.config.salt, cellX, cellZ, 0u );
                const std::uint64_t h1 = hashAnchorCell( config.seed, anchorSet.config.salt, cellX, cellZ, 1u );
                if( remX != static_cast<std::int64_t>( h0 % static_cast<std::uint64_t>(spacing) ) ||
                    remZ != static_cast<std::int64_t>( h1 % static_cast<std::uint64_t>(spacing) ) ) continue;

                double probability = anchorSet.config.chance;
                if( anchorSet.densityField != NO_FIELD )
                    probability *= std::clamp( fieldValueAt( anchorSet.densityField, column, origin, sampled ), 0.0, 1.0 );
                if( anchorSet.biomeIndex != NO_FIELD )
                {
                    const double biomeWeight = biomeWeightAtColumn( anchorSet.biomeIndex, column,
                                                                     origin, biomeWeights );
                    if( !matchesCondition( biomeWeight, anchorSet.config.biomeCondition ) )
                        probability = 0.0;
                    else
                        probability *= std::clamp( biomeWeight, 0.0, 1.0 );
                }
                bool enabled = unitRandom( hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,2u) ) < probability;
                for( const RuntimeAnchorCondition &condition : anchorSet.conditions )
                {
                    if( !enabled ) break;
                    enabled = matchesCondition( fieldValueAt(condition.field,column,origin,sampled), condition.condition );
                }
                if( !enabled ) continue;

                world::BlockAddress anchorPos{};
                std::uint16_t supportBlock = std::numeric_limits<std::uint16_t>::max();
                if( anchorSet.config.surfaceMode == AnchorSurfaceMode::Postprocess )
                {
                    const auto snapped = resolvePostprocessSurface( anchorSet, column );
                    if( !snapped ) continue;
                    if( anchorSet.config.surfaceOffset == 0 ) supportBlock = snapped->blockId;
                    if( !world::tryOffsetBlock( snapped->position, 0,
                            static_cast<std::int64_t>(anchorSet.config.surfaceOffset)+1, 0, anchorPos ) ) continue;
                }
                else
                {
                    const double raw = fieldValueAt( anchorSet.surfaceField, column, origin, sampled );
                    const std::int64_t h = floorToBlock( raw, fields[anchorSet.surfaceField].config().id );
                    if( ( anchorSet.config.surfaceOffset > 0 && h > std::numeric_limits<std::int64_t>::max() - anchorSet.config.surfaceOffset - 1LL ) ||
                        ( anchorSet.config.surfaceOffset < 0 && h < std::numeric_limits<std::int64_t>::min() - anchorSet.config.surfaceOffset - 1LL ) )
                        continue;
                    anchorPos = world::withOriginRelativeY( column, h + anchorSet.config.surfaceOffset + 1LL );
                }
                DecorationAnchor a;
                a.position = anchorPos;
                a.seed = hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,3u);
                a.variant = unitRandom(hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,4u));
                a.supportBlock = supportBlock;
                anchors.push_back(a);
            }
            return anchors;
        }

        bool supportSatisfied( const RuntimeDecoration &decoration, const DecorationAnchor &anchor,
                               const world::BlockAddress &origin,
                               std::span<const std::uint16_t> base ) const
        {
            if( decoration.config.supportBlocks.empty() && decoration.config.supportTags.empty() ) return true;
            if( anchor.supportBlock != std::numeric_limits<std::uint16_t>::max() )
                return anchor.supportBlock < decoration.supportMask.size() && decoration.supportMask[anchor.supportBlock] != 0u;
            world::BlockAddress support;
            if( !world::tryOffsetBlock(anchor.position,0,-1,0,support) ) return false;
            std::size_t index=0;
            if( !localIndexOf(support,origin,index) ) return true;
            const std::uint16_t id=base[index];
            return id < decoration.supportMask.size() && decoration.supportMask[id] != 0u;
        }

        std::vector<BlockProposal> buildDecorationProposals( const RuntimeDecoration &decoration,
            const std::vector<DecorationAnchor> &anchors, const world::BlockAddress &origin,
            std::span<const std::uint16_t> base ) const
        {
            std::vector<BlockProposal> proposals;
            const auto emit = [&]( const world::BlockAddress &point, std::uint16_t blockId ) {
                std::size_t index=0;
                if( localIndexOf(point,origin,index) )
                    proposals.push_back({index,blockId,decoration.config.priority,decoration.order});
            };
            for( const DecorationAnchor &anchor : anchors )
            {
                if( anchor.variant < decoration.config.anchorMin || anchor.variant >= decoration.config.anchorMax ) continue;
                if( decoration.config.type != DecorationType::Structure &&
                    !supportSatisfied(decoration,anchor,origin,base) ) continue;
                if( decoration.config.type == DecorationType::Scatter )
                {
                    emit(anchor.position,decoration.blockId); continue;
                }
                if( decoration.config.type == DecorationType::Column )
                {
                    const std::uint64_t span = static_cast<std::uint64_t>(decoration.config.maxHeight) -
                                               static_cast<std::uint64_t>(decoration.config.minHeight) + 1ULL;
                    const std::int32_t height = decoration.config.minHeight +
                        static_cast<std::int32_t>(mix64(anchor.seed ^ decoration.config.salt)%span);
                    for( std::int32_t dy=0; dy<height; ++dy )
                    {
                        world::BlockAddress p;
                        if( world::tryOffsetBlock(anchor.position,0,dy,0,p) ) emit(p,decoration.blockId);
                    }
                    continue;
                }
                const auto &b=decoration.config.bounds;
                const std::uint64_t structureSeed=mix64(anchor.seed ^ decoration.config.salt);
                for(std::int32_t dx=b.minX;dx<=b.maxX;++dx) for(std::int32_t dy=b.minY;dy<=b.maxY;++dy)
                    for(std::int32_t dz=b.minZ;dz<=b.maxZ;++dz)
                    {
                        world::BlockAddress p;
                        if(!world::tryOffsetBlock(anchor.position,dx,dy,dz,p)) continue;
                        std::size_t dummy=0; if(!localIndexOf(p,origin,dummy)) continue;
                        const double raw=decoration.structureEvaluator->sample3DWithSeed(dx,dy,dz,structureSeed);
                        if(raw<=0.0) continue;
                        const double rounded=std::round(raw);
                        if(std::fabs(raw-rounded)>1e-9) throw std::runtime_error("structure palette result must be integer");
                        const std::int64_t pi=static_cast<std::int64_t>(rounded);
                        if(pi<1 || pi>static_cast<std::int64_t>(decoration.paletteIds.size()))
                            throw std::runtime_error("structure palette index out of range");
                        emit(p,decoration.paletteIds[static_cast<std::size_t>(pi-1)]);
                    }
            }
            return proposals;
        }
    };

    WorldGen::WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                        const world::BlockIdTable &table )
        : mState( std::make_unique<State>( config, blocks, table, nullptr ) ) {}

    WorldGen::WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                        const world::BlockIdTable &table, const world::BiomeRegistry &biomes )
        : mState( std::make_unique<State>( config, blocks, table, &biomes ) ) {}
    WorldGen::~WorldGen() = default;

    std::uint32_t WorldGen::generateChunkIds( const world::ChunkAddress &chunk,
                                               std::span<std::uint16_t> out ) const
    {
        const std::size_t expected=static_cast<std::size_t>(world::chunkVolume());
        if(out.size()!=expected) throw std::invalid_argument("WorldGen::generateChunkIds: wrong output size");
        const world::BlockAddress origin=world::chunkOrigin(chunk);
        std::fill(out.begin(),out.end(),0u);

