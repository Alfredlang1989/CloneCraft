            !detail::tryOffsetAxis( blockAxisZ( base ), dz, z ) ) return false;
        out = fromAxes( x, y, z );
        return true;
    }
    inline BlockAddress offsetBlock( const BlockAddress &base, std::int64_t dx,
                                     std::int64_t dy, std::int64_t dz )
    {
        BlockAddress out{};
        if( !tryOffsetBlock( base, dx, dy, dz, out ) )
            throw std::overflow_error( "BlockAddress sector overflow" );
        return out;
    }

    inline bool tryOffsetChunk( const ChunkAddress &base, std::int64_t dx, std::int64_t dy,
                                std::int64_t dz, ChunkAddress &out ) noexcept
    {
        const BlockAddress bb = chunkOrigin( base );
        detail::AxisAddress x{}, y{}, z{};
        if( !detail::tryOffsetChunkAxis( blockAxisX( bb ), dx, x ) ||
            !detail::tryOffsetChunkAxis( blockAxisY( bb ), dy, y ) ||
            !detail::tryOffsetChunkAxis( blockAxisZ( bb ), dz, z ) ) return false;
        out = fromAxes( x, y, z ).chunk;
        return true;
    }
    inline ChunkAddress offsetChunk( const ChunkAddress &base, std::int64_t dx,
                                     std::int64_t dy, std::int64_t dz )
    {
        ChunkAddress out{};
        if( !tryOffsetChunk( base, dx, dy, dz, out ) )
            throw std::overflow_error( "ChunkAddress sector overflow" );
        return out;
    }

    inline bool tryOffsetGroup( const GroupAddress &base, std::int64_t dx, std::int64_t dy,
                                std::int64_t dz, GroupAddress &out ) noexcept
    {
        const BlockAddress bb = chunkOrigin( { base, {} } );
        detail::AxisAddress x{}, y{}, z{};
        if( !detail::tryOffsetGroupAxis( blockAxisX( bb ), dx, x ) ||
            !detail::tryOffsetGroupAxis( blockAxisY( bb ), dy, y ) ||
            !detail::tryOffsetGroupAxis( blockAxisZ( bb ), dz, z ) ) return false;
        out = fromAxes( x, y, z ).chunk.group;
        return true;
    }
    inline GroupAddress offsetGroup( const GroupAddress &base, std::int64_t dx,
                                     std::int64_t dy, std::int64_t dz )
    {
        GroupAddress out{};
        if( !tryOffsetGroup( base, dx, dy, dz, out ) )
            throw std::overflow_error( "GroupAddress sector overflow" );
        return out;
    }

    inline bool tryOffsetHierarchy( const BlockAddress &base, HierarchyLevel level,
                                    std::int64_t dx, std::int64_t dy, std::int64_t dz,
                                    BlockAddress &out ) noexcept
    {
        detail::AxisAddress x{}, y{}, z{};
        if( !detail::tryOffsetHierarchyAxis( blockAxisX( base ), level, dx, x ) ||
            !detail::tryOffsetHierarchyAxis( blockAxisY( base ), level, dy, y ) ||
            !detail::tryOffsetHierarchyAxis( blockAxisZ( base ), level, dz, z ) )
            return false;
        out = fromAxes( x, y, z );
        return true;
    }

    inline BlockAddress offsetHierarchy( const BlockAddress &base, HierarchyLevel level,
                                         std::int64_t dx, std::int64_t dy,
                                         std::int64_t dz )
    {
        BlockAddress out{};
        if( !tryOffsetHierarchy( base, level, dx, dy, dz, out ) )
            throw std::overflow_error( "hierarchical address navigation crossed SectorCoord range" );
        return out;
    }

    inline bool blockDeltaWithin( const BlockAddress &position, const BlockAddress &origin,
                                  std::int64_t maxAbs, RelativeI64 &out ) noexcept
    {
        return detail::blockAxisDeltaWithin( blockAxisX( position ), blockAxisX( origin ), maxAbs, out.x ) &&
               detail::blockAxisDeltaWithin( blockAxisY( position ), blockAxisY( origin ), maxAbs, out.y ) &&
               detail::blockAxisDeltaWithin( blockAxisZ( position ), blockAxisZ( origin ), maxAbs, out.z );
    }
    inline bool chunkDeltaWithin( const ChunkAddress &position, const ChunkAddress &origin,
                                  std::int64_t maxAbs, RelativeI64 &out ) noexcept
    {
        const BlockAddress p = chunkOrigin( position ), o = chunkOrigin( origin );
        return detail::chunkAxisDeltaWithin( blockAxisX( p ), blockAxisX( o ), maxAbs, out.x ) &&
               detail::chunkAxisDeltaWithin( blockAxisY( p ), blockAxisY( o ), maxAbs, out.y ) &&
               detail::chunkAxisDeltaWithin( blockAxisZ( p ), blockAxisZ( o ), maxAbs, out.z );
    }
    inline bool groupDeltaWithin( const GroupAddress &position, const GroupAddress &origin,
                                  std::int64_t maxAbs, RelativeI64 &out ) noexcept
    {
        const BlockAddress p = chunkOrigin( { position, {} } ), o = chunkOrigin( { origin, {} } );
        return detail::groupAxisDeltaWithin( blockAxisX( p ), blockAxisX( o ), maxAbs, out.x ) &&
               detail::groupAxisDeltaWithin( blockAxisY( p ), blockAxisY( o ), maxAbs, out.y ) &&
               detail::groupAxisDeltaWithin( blockAxisZ( p ), blockAxisZ( o ), maxAbs, out.z );
    }

    inline bool chunkWithinChebyshev( const ChunkAddress &a, const ChunkAddress &b,
                                      std::int64_t radius ) noexcept
    {
        RelativeI64 d{};
        return radius >= 0 && chunkDeltaWithin( a, b, radius, d );
    }

    inline std::uint32_t blockIndex( const LocalBlockCoord &local )
    {
        if( !validLocal( local.x, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( local.y, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( local.z, BLOCKS_PER_CHUNK_EDGE ) )
            throw std::out_of_range( "blockIndex requires canonical local block coordinate" );
        return static_cast<std::uint32_t>(
            ( local.x * BLOCKS_PER_CHUNK_EDGE + local.y ) * BLOCKS_PER_CHUNK_EDGE + local.z );
    }
    inline std::uint32_t chunkIndex( const LocalChunkCoord &local )
    {
        if( !validLocal( local.x, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( local.y, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( local.z, CHUNKS_PER_GROUP_EDGE ) )
            throw std::out_of_range( "chunkIndex requires canonical local chunk coordinate" );
        return static_cast<std::uint32_t>(
            ( local.x * CHUNKS_PER_GROUP_EDGE + local.y ) * CHUNKS_PER_GROUP_EDGE + local.z );
    }

    inline BlockAddress fromOriginOffset( std::int64_t x, std::int64_t y, std::int64_t z )
    {
        return offsetBlock( originBlockAddress(), x, y, z );
    }

    inline BlockAddress withOriginRelativeY( const BlockAddress &column, std::int64_t y )
    {
        const BlockAddress oy = fromOriginOffset( 0, y, 0 );
        return fromAxes( blockAxisX( column ), blockAxisY( oy ), blockAxisZ( column ) );
    }

    inline RelativeI64 chunkOriginRelativeToGroup( const ChunkAddress &chunk,
                                                   const GroupAddress &renderGroup )
    {
        RelativeI64 gd{};
        constexpr std::int64_t kMaxBackendGroupDelta = 64;
        if( !groupDeltaWithin( chunk.group, renderGroup, kMaxBackendGroupDelta, gd ) )
            throw std::overflow_error( "render-group origin is not local" );
        return { gd.x * BLOCKS_PER_GROUP_EDGE + chunk.chunk.x * BLOCKS_PER_CHUNK_EDGE,
                 gd.y * BLOCKS_PER_GROUP_EDGE + chunk.chunk.y * BLOCKS_PER_CHUNK_EDGE,
                 gd.z * BLOCKS_PER_GROUP_EDGE + chunk.chunk.z * BLOCKS_PER_CHUNK_EDGE };
    }
} // namespace world
