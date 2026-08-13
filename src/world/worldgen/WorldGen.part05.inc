                if( !columnEnabledAt( pass ) ) return false;
                const auto y = world::blockAxisY( point );
                switch( pass.config.type )
                {
                case PassType::FillBelow:
                    return y <= originHeightAxis( floorToBlock( sample2D( pass.field ), fields[pass.field].config().id ) );
                case PassType::Surface:
                    return y == originHeightAxis( shiftedSurface( sample2D( pass.field ),
                        fields[pass.field].config().id, pass.config.surfaceOffset ) );
                case PassType::SurfaceLayer:
                {
                    const std::int64_t topH = shiftedSurface( sample2D( pass.surfaceField ),
                        fields[pass.surfaceField].config().id, pass.config.surfaceOffset );
                    const auto top = originHeightAxis( topH );
                    if( y > top ) return false;
                    if( pass.bottomField != NO_FIELD )
                    {
                        const std::int64_t bottomH = shiftedSurface(
                            sample2D( pass.bottomField ), fields[pass.bottomField].config().id,
                            pass.config.bottomOffset );
                        const auto bottom = originHeightAxis( bottomH );
                        return y >= bottom;
                    }
                    std::int64_t depth = pass.config.thickness;
                    if( pass.thicknessField != NO_FIELD )
                        depth = std::max<std::int64_t>( 0, floorToBlock(
                            sample2D( pass.thicknessField ), fields[pass.thicknessField].config().id ) );
                    if( depth <= 0 ) return false;
                    std::int64_t delta = 0;
                    return world::detail::blockAxisDeltaWithin( y, top, depth - 1, delta );
                }
                case PassType::Volume:
                    return matchesCondition( fields[pass.field].sample3D( point ), pass.config.condition );
                }
                return false;
            };
            const auto blockAt = [&]( const world::BlockAddress &point ) {
                std::uint16_t current = 0u;
                const auto applyStage = [&]( const std::vector<std::size_t> &order ) {
                    for( const std::size_t passIndex : order )
                    {
                        const RuntimePass &pass = passes[passIndex];
                        if( appliesAt( pass, point ) && canReplace( current, pass ) )
                            current = pass.blockId;
                    }
                };
                for( const std::vector<std::size_t> &order : stageMergeOrders )
                    applyStage( order );
                return current;
            };

            std::int64_t highestTop = floorToBlock( sample2D( anchorSet.surfaceField ),
                                                    fields[anchorSet.surfaceField].config().id );
            for( const RuntimePass &pass : passes )
            {
                if( pass.config.type == PassType::Volume || !columnEnabledAt( pass ) ) continue;
                std::int64_t top = highestTop;
                if( pass.config.type == PassType::FillBelow )
                    top = floorToBlock( sample2D( pass.field ), fields[pass.field].config().id );
                else if( pass.config.type == PassType::Surface )
                    top = shiftedSurface( sample2D( pass.field ), fields[pass.field].config().id,
                                          pass.config.surfaceOffset );
                else if( pass.config.type == PassType::SurfaceLayer )
                {
                    if( pass.bottomField == NO_FIELD )
                    {
                        std::int64_t depth = pass.config.thickness;
                        if( pass.thicknessField != NO_FIELD )
                            depth = std::max<std::int64_t>( 0, floorToBlock(
                                sample2D( pass.thicknessField ), fields[pass.thicknessField].config().id ) );
                        if( depth <= 0 ) continue;
                    }
                    top = shiftedSurface( sample2D( pass.surfaceField ), fields[pass.surfaceField].config().id,
                                          pass.config.surfaceOffset );
                }
                highestTop = std::max( highestTop, top );
            }

            world::BlockAddress point = world::withOriginRelativeY( column, highestTop );
            for( std::int32_t drop = 0; drop <= anchorSet.config.maxSurfaceDrop; ++drop )
            {
                const std::uint16_t id = blockAt( point );
                if( id != 0u ) return TerrainSurfaceSample{ point, id };
                if( drop == anchorSet.config.maxSurfaceDrop ) break;
                world::BlockAddress next;
                if( !world::tryOffsetBlock( point, 0, -1, 0, next ) ) break;
                point = next;
            }
            return std::nullopt;
        }

        std::vector<BlockProposal> buildPassProposals( const RuntimePass &pass,
            const std::vector<SampledField> &sampled, const BiomeWeightGrid &biomeWeights,
            const world::BlockAddress &origin ) const
        {
            std::vector<BlockProposal> proposals;
            const auto emit = [&]( std::int64_t lx, std::int64_t ly, std::int64_t lz ) {
                proposals.push_back( { denseIndex( lx, ly, lz ), pass.blockId,
                                       pass.config.priority, pass.order } );
            };

            if( pass.config.type == PassType::Volume )
            {
                const SampledField &field = sampled[pass.field];
                for( std::int64_t lx=0; lx<EDGE; ++lx ) for( std::int64_t lz=0; lz<EDGE; ++lz )
                {
                    if( !columnEnabled( pass, sampled, biomeWeights, lx, lz ) ) continue;
                    for( std::int64_t ly=0; ly<EDGE; ++ly )
                        if( matchesCondition( field.at3D(lx,ly,lz), pass.config.condition ) ) emit(lx,ly,lz);
                }
                return proposals;
            }

            for( std::int64_t lx=0; lx<EDGE; ++lx ) for( std::int64_t lz=0; lz<EDGE; ++lz )
            {
                if( !columnEnabled( pass, sampled, biomeWeights, lx, lz ) ) continue;
                std::int64_t topH = 0;
                std::int64_t bottomH = 0;
                std::int64_t depth = 1;
                bool boundedByBottom = false;
                if( pass.config.type == PassType::SurfaceLayer )
                {
                    const SampledField &surface = sampled[pass.surfaceField];
                    topH = shiftedSurface( surface.at2D(lx,lz), surface.id, pass.config.surfaceOffset );
                    if( pass.bottomField != NO_FIELD )
                    {
                        const SampledField &bottom = sampled[pass.bottomField];
                        bottomH = shiftedSurface( bottom.at2D(lx,lz), bottom.id,
                                                  pass.config.bottomOffset );
                        boundedByBottom = true;
                        if( bottomH > topH ) continue;
                    }
                    else
                    {
                        depth = pass.config.thickness;
                        if( pass.thicknessField != NO_FIELD )
                        {
                            const SampledField &t = sampled[pass.thicknessField];
                            depth = std::max<std::int64_t>( 0, floorToBlock( t.at2D(lx,lz), t.id ) );
                        }
                        if( depth <= 0 ) continue;
                    }
                }
                else
                {
                    const SampledField &field = sampled[pass.field];
                    topH = pass.config.type == PassType::Surface
                        ? shiftedSurface( field.at2D(lx,lz), field.id, pass.config.surfaceOffset )
                        : floorToBlock( field.at2D(lx,lz), field.id );
                }
                const auto topY = originHeightAxis( topH );
                for( std::int64_t ly=0; ly<EDGE; ++ly )
                {
                    const world::BlockAddress point = world::blockAt( origin.chunk, { lx, ly, lz } );
                    const auto y = world::blockAxisY( point );
                    bool applies = false;
                    if( pass.config.type == PassType::FillBelow ) applies = y <= topY;
                    else if( pass.config.type == PassType::Surface ) applies = y == topY;
                    else
                    {
                        if( y <= topY )
                        {
                            if( boundedByBottom )
                            {
                                const auto bottomY = originHeightAxis( bottomH );
                                applies = y >= bottomY;
                            }
                            else
                            {
                                std::int64_t d=0;
                                applies = world::detail::blockAxisDeltaWithin( y, topY, depth-1, d );
