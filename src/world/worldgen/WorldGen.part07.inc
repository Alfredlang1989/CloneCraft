        std::vector<SampledField> sampled(mState->fields.size());
        mState->executor->run(mState->field2DIndices.size(),[&](std::size_t job){
            const std::size_t i=mState->field2DIndices[job]; sampled[i]=mState->fields[i].sampleChunk(chunk);
        });

        State::BiomeWeightGrid biomeWeights;
        mState->applyBiomeTerrain( chunk, sampled, biomeWeights );

        std::vector<std::vector<DecorationAnchor>> anchors(mState->anchorSets.size());
        if(!mState->decorations.empty())
            mState->executor->run(mState->anchorSets.size(),[&](std::size_t i){
                anchors[i]=mState->buildAnchors(mState->anchorSets[i],origin,sampled,biomeWeights);
            });

        const auto decorationTouchesChunkY=[&](){
            const auto chunkMin=world::blockAxisY(origin);
            const auto chunkMax=world::blockAxisY(world::blockAt(chunk,{0,EDGE-1,0}));
            for(std::size_t i=0;i<mState->anchorSets.size();++i)
            {
                const auto &bounds=mState->anchorSets[i].influenceBounds;
                for(const DecorationAnchor &a: anchors[i])
                {
                    world::BlockAddress lo,hi;
                    if(!world::tryOffsetBlock(a.position,0,bounds.minY,0,lo) ||
                       !world::tryOffsetBlock(a.position,0,bounds.maxY,0,hi)) continue;
                    if(world::blockAxisY(hi)>=chunkMin && world::blockAxisY(lo)<=chunkMax) return true;
                }
            }
            return false;
        };

        bool aboveBaseTerrain=false;
        if(!mState->volumeCanCreateInAir)
        {
            std::int64_t highest=std::numeric_limits<std::int64_t>::min();
            for(const RuntimePass &pass:mState->passes)
            {
                if(pass.config.type==PassType::Volume) continue;
                const SampledField *topField=nullptr; std::int32_t offset=0;
                if(pass.config.type==PassType::SurfaceLayer){topField=&sampled[pass.surfaceField];offset=pass.config.surfaceOffset;}
                else {topField=&sampled[pass.field]; if(pass.config.type==PassType::Surface) offset=pass.config.surfaceOffset;}
                for(std::int64_t lx=0;lx<EDGE;++lx) for(std::int64_t lz=0;lz<EDGE;++lz)
                {
                    if(!mState->columnEnabled(pass,sampled,biomeWeights,lx,lz)) continue;
                    highest=std::max(highest,mState->shiftedSurface(topField->at2D(lx,lz),topField->id,offset));
                }
            }
            aboveBaseTerrain=world::blockAxisY(origin)>originHeightAxis(highest);
            if(aboveBaseTerrain && !decorationTouchesChunkY()) return 0u;
        }

        if(!aboveBaseTerrain)
        {
            for( const std::size_t i : mState->field3DIndices )
            {
                sampled[i] = SampledField{ mState->fields[i].config().id, FieldDimension::D3, {} };
                sampled[i].values.resize( static_cast<std::size_t>( world::chunkVolume() ) );
            }
            const std::size_t sliceJobs = mState->field3DIndices.size() *
                                          static_cast<std::size_t>( EDGE );
            mState->executor->run(sliceJobs,[&](std::size_t job){
                const std::size_t fieldJob = job / static_cast<std::size_t>( EDGE );
                const std::int64_t lx = static_cast<std::int64_t>(
                    job % static_cast<std::size_t>( EDGE ) );
                const std::size_t i = mState->field3DIndices[fieldJob];
                mState->fields[i].sampleChunkXSlice( chunk, lx, sampled[i] );
            });
            std::vector<std::vector<BlockProposal>> proposals(mState->passes.size());
            mState->executor->run(mState->passes.size(),[&](std::size_t i){
                proposals[i]=mState->buildPassProposals(mState->passes[i],sampled,biomeWeights,origin);
            });
            const auto applyStage = [&]( const std::vector<std::size_t> &order ) {
                for( const std::size_t passIndex : order )
                {
                    const RuntimePass &pass = mState->passes[passIndex];
                    for( const BlockProposal &proposal : proposals[passIndex] )
                    {
                        if( mState->canReplace( out[proposal.localIndex], pass ) )
                            out[proposal.localIndex] = proposal.blockId;
                    }
                }
            };
            // Hard worldgen barriers are fully data-defined by stage.json.
            // Decoration remains a separate post-mutation system and is merged
            // only after every configured block-mutating stage has completed.
            for( const std::vector<std::size_t> &order : mState->stageMergeOrders )
                applyStage( order );
        }

        if(!mState->decorations.empty())
        {
            std::vector<std::vector<BlockProposal>> dprops(mState->decorations.size());
            mState->executor->run(mState->decorations.size(),[&](std::size_t i){
                const auto &d=mState->decorations[i];
                dprops[i]=mState->buildDecorationProposals(d,anchors[d.anchorSet],origin,out);
            });
            for(const std::size_t di:mState->decorationMergeOrder)
            {
                const auto &d=mState->decorations[di];
                for(const BlockProposal &p:dprops[di])
                    if(p.localIndex<out.size() && p.blockId!=0u && out[p.localIndex]<d.replaceMask.size() &&
                       d.replaceMask[out[p.localIndex]]!=0u) out[p.localIndex]=p.blockId;
            }
        }
        std::uint32_t nonAir=0; for(std::uint16_t id:out) nonAir += id!=0u ? 1u:0u; return nonAir;
    }

    void WorldGen::generateChunk( const world::ChunkAddress &chunk, std::vector<BlockDelta> &out ) const
    {
        std::vector<std::uint16_t> ids(static_cast<std::size_t>(world::chunkVolume()),0u);
        const std::uint32_t nonAir=generateChunkIds(chunk,ids); out.clear(); out.reserve(nonAir);
        for(std::int64_t lx=0;lx<EDGE;++lx) for(std::int64_t ly=0;ly<EDGE;++ly) for(std::int64_t lz=0;lz<EDGE;++lz)
        {
            const std::uint16_t id=ids[denseIndex(lx,ly,lz)]; if(id==0u) continue;
            out.push_back({world::blockAt(chunk,{lx,ly,lz}),mState->table.idOf(id)});
        }
    }

    std::int64_t WorldGen::surfaceHeight( const world::BlockAddress &column ) const
    {
        const LuaFieldEvaluator &surface=mState->fields[mState->surfaceField];
        const double height = !mState->biomeTerrains.empty()
            ? mState->adjustedSurfaceAt( column )
            : surface.sample2D( column );
        return floorToBlock( height, surface.config().id );
    }
    std::vector<BiomeWeightSample> WorldGen::biomeWeights( const world::BlockAddress &column ) const
    {
        const std::vector<double> weights = mState->biomeWeightsAt( column );
        std::vector<BiomeWeightSample> out;
        out.reserve( weights.size() );
        for( std::size_t i = 0; i < weights.size(); ++i )
            out.push_back( { mState->biomeTerrains[i].id, weights[i] } );
        return out;
    }

    std::uint32_t WorldGen::workerThreads() const { return mState->executor->workerCount(); }
    std::size_t WorldGen::fieldCount() const { return mState->fields.size(); }
    std::size_t WorldGen::stageCount() const { return mState->stageMergeOrders.size(); }
    std::size_t WorldGen::passCount() const { return mState->passes.size(); }
    std::size_t WorldGen::anchorSetCount() const { return mState->anchorSets.size(); }
    std::size_t WorldGen::decorationPassCount() const { return mState->decorations.size(); }
} // namespace worldgen
