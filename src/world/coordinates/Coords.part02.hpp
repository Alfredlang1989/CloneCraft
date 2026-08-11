        }

        template <typename OffsetFn>
        inline bool boundedAxisDelta( const AxisAddress &position, const AxisAddress &origin,
                                      std::int64_t maxAbs, std::int64_t &out,
                                      OffsetFn &&offsetFn ) noexcept
        {
            if( maxAbs < 0 ) return false;
            if( position == origin ) { out = 0; return true; }

            clonecraft_i128 lo = -static_cast<clonecraft_i128>( maxAbs );
            clonecraft_i128 hi = static_cast<clonecraft_i128>( maxAbs );
            while( lo <= hi )
            {
                const clonecraft_i128 midWide = lo + ( hi - lo ) / 2;
                const std::int64_t mid = static_cast<std::int64_t>( midWide );
                AxisAddress candidate{};
                if( !offsetFn( origin, mid, candidate ) )
                {
                    if( mid < 0 ) lo = midWide + 1;
                    else hi = midWide - 1;
                    continue;
                }
                if( candidate == position ) { out = mid; return true; }
                if( candidate < position ) lo = midWide + 1;
                else hi = midWide - 1;
            }
            return false;
        }

        inline bool blockAxisDeltaWithin( const AxisAddress &position, const AxisAddress &origin,
                                          std::int64_t maxAbs, std::int64_t &out ) noexcept
        {
            if( position.sector == origin.sector && position.region == origin.region &&
                position.section == origin.section && position.group == origin.group )
            {
                const clonecraft_i128 delta =
                    ( static_cast<clonecraft_i128>( position.chunk - origin.chunk ) *
                      BLOCKS_PER_CHUNK_EDGE ) +
                    ( position.block - origin.block );
                if( delta >= -static_cast<clonecraft_i128>( maxAbs ) &&
                    delta <= static_cast<clonecraft_i128>( maxAbs ) )
                {
                    out = static_cast<std::int64_t>( delta );
                    return true;
                }
                return false;
            }
            return boundedAxisDelta( position, origin, maxAbs, out,
                                     []( const AxisAddress &a, std::int64_t d,
                                         AxisAddress &r ) { return tryOffsetAxis( a, d, r ); } );
        }

        inline bool chunkAxisDeltaWithin( const AxisAddress &position, const AxisAddress &origin,
                                          std::int64_t maxAbs, std::int64_t &out ) noexcept
        {
            if( position.sector == origin.sector && position.region == origin.region &&
                position.section == origin.section && position.group == origin.group )
            {
                const std::int64_t delta = position.chunk - origin.chunk;
                if( delta >= -maxAbs && delta <= maxAbs ) { out = delta; return true; }
                return false;
            }
            return boundedAxisDelta( position, origin, maxAbs, out,
                                     []( const AxisAddress &a, std::int64_t d,
                                         AxisAddress &r ) { return tryOffsetChunkAxis( a, d, r ); } );
        }

        inline bool groupAxisDeltaWithin( const AxisAddress &position, const AxisAddress &origin,
                                          std::int64_t maxAbs, std::int64_t &out ) noexcept
        {
            if( position.sector == origin.sector && position.region == origin.region &&
                position.section == origin.section )
            {
                const std::int64_t delta = position.group - origin.group;
                if( delta >= -maxAbs && delta <= maxAbs ) { out = delta; return true; }
                return false;
            }
            return boundedAxisDelta( position, origin, maxAbs, out,
                                     []( const AxisAddress &a, std::int64_t d,
                                         AxisAddress &r ) { return tryOffsetGroupAxis( a, d, r ); } );
        }
    } // namespace detail

    inline detail::AxisAddress blockAxisX( const BlockAddress &a ) noexcept
    { return { a.chunk.group.sector.x, a.chunk.group.region.x, a.chunk.group.section.x,
               a.chunk.group.group.x, a.chunk.chunk.x, a.block.x }; }
    inline detail::AxisAddress blockAxisY( const BlockAddress &a ) noexcept
    { return { a.chunk.group.sector.y, a.chunk.group.region.y, a.chunk.group.section.y,
               a.chunk.group.group.y, a.chunk.chunk.y, a.block.y }; }
    inline detail::AxisAddress blockAxisZ( const BlockAddress &a ) noexcept
    { return { a.chunk.group.sector.z, a.chunk.group.region.z, a.chunk.group.section.z,
               a.chunk.group.group.z, a.chunk.chunk.z, a.block.z }; }

    inline BlockAddress fromAxes( const detail::AxisAddress &x, const detail::AxisAddress &y,
                                  const detail::AxisAddress &z ) noexcept
    {
        return { { { { x.sector, y.sector, z.sector },
                       { x.region, y.region, z.region },
                       { x.section, y.section, z.section },
                       { x.group, y.group, z.group } },
                     { x.chunk, y.chunk, z.chunk } },
                   { x.block, y.block, z.block } };
    }

    inline ChunkAddress chunkOf( const BlockAddress &a ) noexcept { return a.chunk; }
    inline GroupAddress groupOf( const ChunkAddress &a ) noexcept { return a.group; }
    inline GroupAddress groupOfBlock( const BlockAddress &a ) noexcept { return a.chunk.group; }
    inline LocalBlockCoord localInChunk( const BlockAddress &a ) noexcept { return a.block; }
    inline LocalChunkCoord localInGroup( const ChunkAddress &a ) noexcept { return a.chunk; }
    inline LocalGroupBlockCoord localBlockInGroup( const BlockAddress &a ) noexcept
    {
        return { a.chunk.chunk.x * BLOCKS_PER_CHUNK_EDGE + a.block.x,
                 a.chunk.chunk.y * BLOCKS_PER_CHUNK_EDGE + a.block.y,
                 a.chunk.chunk.z * BLOCKS_PER_CHUNK_EDGE + a.block.z };
    }

    inline BlockAddress chunkOrigin( const ChunkAddress &a ) noexcept { return { a, {} }; }
    inline ChunkAddress chunkAt( const GroupAddress &g, const LocalChunkCoord &c ) noexcept
    { return { g, c }; }
    inline BlockAddress blockAt( const ChunkAddress &c, const LocalBlockCoord &b ) noexcept
    { return { c, b }; }

    inline bool tryOffsetBlock( const BlockAddress &base, std::int64_t dx, std::int64_t dy,
                                std::int64_t dz, BlockAddress &out ) noexcept
    {
        detail::AxisAddress x{}, y{}, z{};
        if( !detail::tryOffsetAxis( blockAxisX( base ), dx, x ) ||
            !detail::tryOffsetAxis( blockAxisY( base ), dy, y ) ||
