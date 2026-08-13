        for( auto &c : counts ) c.store( 0 );
        std::vector<double> contributionSums( explicitBiomes.size() + 1u, 0.0 );
        std::mutex contributionMutex;

        std::atomic<int> nextRow{ 0 };
        std::atomic<int> finishedRows{ 0 };
        std::mutex printMutex;

        auto render = [&] {
            std::vector<double> localContributions( explicitBiomes.size() + 1u, 0.0 );
            for( ;; )
            {
                const int py = nextRow.fetch_add( 1 );
                if( py >= args.height ) break;
                const std::int64_t chunksPerPixel = args.blocksPerPixel / world::BLOCKS_PER_CHUNK_EDGE;
                const std::int64_t chunkZ = args.centerChunkZ +
                    ( static_cast<std::int64_t>( py ) - args.height / 2 ) * chunksPerPixel;
                for( int px = 0; px < args.width; ++px )
                {
                    const std::int64_t chunkX = args.centerChunkX +
                        ( static_cast<std::int64_t>( px ) - args.width / 2 ) * chunksPerPixel;
                    const world::ChunkAddress chunk = world::offsetChunk( world::originChunkAddress(), chunkX, 0, chunkZ );
                    const world::BlockAddress point = world::blockAt(
                        chunk, { world::BLOCKS_PER_CHUNK_EDGE / 2, 0, world::BLOCKS_PER_CHUNK_EDGE / 2 } );

                    double explicitSum = 0.0;
                    double bestWeight = -1.0;
                    std::size_t best = explicitBiomes.size(); // fallback
                    std::vector<double> biomeWeights( explicitBiomes.size(), 0.0 );
                    for( std::size_t i = 0; i < explicitBiomes.size(); ++i )
                    {
                        const double w = std::clamp( explicitBiomes[i].field->sample2D( point ), 0.0, 1.0 );
                        biomeWeights[i] = w;
                        explicitSum += w;
                        if( w > bestWeight )
                        {
                            bestWeight = w;
                            best = i;
                        }
                    }
                    const double fallbackWeight = std::max( 0.0, 1.0 - explicitSum );
                    const double normalizedTotal = std::max( explicitSum + fallbackWeight, 1.0e-12 );
                    for( std::size_t i = 0; i < explicitBiomes.size(); ++i )
                        localContributions[i] += biomeWeights[i] / normalizedTotal;
                    localContributions[explicitBiomes.size()] += fallbackWeight / normalizedTotal;

                    if( fallbackWeight >= bestWeight )
                    {
                        bestWeight = fallbackWeight;
                        best = explicitBiomes.size();
                    }

                    Rgb color{};
                    if( args.blend )
                    {
                        const double totalWeight = explicitSum + fallbackWeight;
                        const Rgb fallbackColor = biomeColor( fallbackBiome->id );
                        double rr = fallbackWeight * fallbackColor.r;
                        double gg = fallbackWeight * fallbackColor.g;
                        double bb = fallbackWeight * fallbackColor.b;
                        for( std::size_t i = 0; i < explicitBiomes.size(); ++i )
                        {
                            rr += biomeWeights[i] * explicitBiomes[i].color.r;
                            gg += biomeWeights[i] * explicitBiomes[i].color.g;
                            bb += biomeWeights[i] * explicitBiomes[i].color.b;
                        }
                        const double denom = std::max( totalWeight, 1.0e-12 );
                        color = {
                            static_cast<std::uint8_t>( std::clamp( std::lround( rr / denom ), 0l, 255l ) ),
                            static_cast<std::uint8_t>( std::clamp( std::lround( gg / denom ), 0l, 255l ) ),
                            static_cast<std::uint8_t>( std::clamp( std::lround( bb / denom ), 0l, 255l ) )
                        };
                    }
                    else
                    {
                        color = best == explicitBiomes.size()
                            ? biomeColor( fallbackBiome->id )
                            : explicitBiomes[best].color;

                        // Reveal weak transition areas without inventing a new biome:
                        // lower dominance is gently mixed toward neutral grassland.
                        color = mix( biomeColor( fallbackBiome->id ), color,
                                     std::clamp( 0.35 + bestWeight * 0.85, 0.0, 1.0 ) );
                    }

                    bool ocean = false;
                    if( oceanField )
                    {
                        const double o = oceanField->sample2D( point );
                        ocean = o > 0.45;
                        if( ocean )
                        {
                            const double deep = std::clamp( ( o - 0.45 ) / 0.55, 0.0, 1.0 );
                            color = mix( { 55, 120, 190 }, { 20, 55, 120 }, deep );
                        }
                    }

                    bool lake = false;
                    if( !ocean && lakeField )
                    {
                        const double l = lakeField->sample2D( point );
                        lake = l > 0.48;
                        if( lake ) color = { 54, 128, 202 };
                    }

                    bool river = false;
                    if( !ocean && !lake && riverField )
                    {
                        const double d = riverField->sample2D( point );
                        river = d < 0.030; // exact current river_water pass threshold
                        if( river ) color = { 41, 103, 187 };
                    }

                    bool snow = false;
                    if( !ocean && !lake && !river && snowField )
                    {
                        const double s = snowField->sample2D( point );
                        if( s > 0.57 )
                        {
                            snow = true;
                            const double t = std::clamp( ( s - 0.57 ) / 0.35, 0.25, 0.85 );
                            color = mix( color, { 238, 242, 240 }, t );
                        }
                    }

                    const std::size_t out = ( static_cast<std::size_t>( py ) * args.width + px ) * 3u;
                    image[out + 0] = color.r;
                    image[out + 1] = color.g;
                    image[out + 2] = color.b;

                    counts[best].fetch_add( 1, std::memory_order_relaxed );
                    if( river ) counts[explicitBiomes.size() + 1u].fetch_add( 1, std::memory_order_relaxed );
                    if( snow ) counts[explicitBiomes.size() + 2u].fetch_add( 1, std::memory_order_relaxed );
                    if( ocean ) counts[explicitBiomes.size() + 3u].fetch_add( 1, std::memory_order_relaxed );
                    if( lake ) counts[explicitBiomes.size() + 4u].fetch_add( 1, std::memory_order_relaxed );
                }
                const int done = finishedRows.fetch_add( 1 ) + 1;
                if( done % std::max( 1, args.height / 20 ) == 0 || done == args.height )
                {
                    std::scoped_lock lock( printMutex );
