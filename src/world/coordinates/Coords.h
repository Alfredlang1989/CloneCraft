#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "world/coordinates/CoordMath.h"

namespace world
{
#if defined(__SIZEOF_INT128__)
    __extension__ typedef __int128 clonecraft_i128;
#else
#error "Clonecraft hierarchical phase-1 coordinates require compiler support for 128-bit integers"
#endif
#ifndef CLONECRAFT_CHUNK_EDGE
#define CLONECRAFT_CHUNK_EDGE 16
#endif
#ifndef CLONECRAFT_GROUP_EDGE
#define CLONECRAFT_GROUP_EDGE 16
#endif
#ifndef CLONECRAFT_REGION_EDGE
#define CLONECRAFT_REGION_EDGE 16
#endif
#ifndef CLONECRAFT_SECTOR_EDGE
#define CLONECRAFT_SECTOR_EDGE 16
#endif

    // Physical storage radices. These remain small because a Chunk and ChunkGroup
    // are materialized containers.
    inline constexpr std::int64_t BLOCKS_PER_CHUNK_EDGE = CLONECRAFT_CHUNK_EDGE;
    inline constexpr std::int64_t CHUNKS_PER_GROUP_EDGE = CLONECRAFT_GROUP_EDGE;

    // Logical address radices. Phase 1 keeps both at 16. Coordinate arithmetic is
    // deliberately written without flattening them so future builds can raise
    // either radix close to the positive int64 range without changing the model.
    inline constexpr std::int64_t GROUPS_PER_REGION_EDGE = CLONECRAFT_REGION_EDGE;
    inline constexpr std::int64_t REGIONS_PER_SECTOR_EDGE = CLONECRAFT_SECTOR_EDGE;

    inline constexpr std::int64_t BLOCKS_PER_GROUP_EDGE =
        BLOCKS_PER_CHUNK_EDGE * CHUNKS_PER_GROUP_EDGE;

    static_assert( BLOCKS_PER_CHUNK_EDGE > 0 );
    static_assert( CHUNKS_PER_GROUP_EDGE > 0 );
    static_assert( GROUPS_PER_REGION_EDGE > 0 );
    static_assert( REGIONS_PER_SECTOR_EDGE > 0 );
    static_assert( BLOCKS_PER_CHUNK_EDGE * BLOCKS_PER_CHUNK_EDGE * BLOCKS_PER_CHUNK_EDGE <=
                   static_cast<std::int64_t>( std::numeric_limits<std::uint32_t>::max() ) );
    static_assert( CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE <=
                   static_cast<std::int64_t>( std::numeric_limits<std::uint32_t>::max() ) );

    struct SectorCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const SectorCoord &, const SectorCoord & ) = default;
    };
    struct LocalRegionCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalRegionCoord &, const LocalRegionCoord & ) = default;
    };
    struct LocalGroupCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalGroupCoord &, const LocalGroupCoord & ) = default;
    };
    struct LocalChunkCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalChunkCoord &, const LocalChunkCoord & ) = default;
    };
    struct LocalBlockCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalBlockCoord &, const LocalBlockCoord & ) = default;
    };
    struct LocalGroupBlockCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalGroupBlockCoord &,
                                           const LocalGroupBlockCoord & ) = default;
    };

    struct GroupAddress
    {
        SectorCoord sector{};
        LocalRegionCoord region{};
        LocalGroupCoord group{};
        friend constexpr auto operator<=>( const GroupAddress &, const GroupAddress & ) = default;
    };
    struct ChunkAddress
    {
        GroupAddress group{};
        LocalChunkCoord chunk{};
        friend constexpr auto operator<=>( const ChunkAddress &, const ChunkAddress & ) = default;
    };
    struct BlockAddress
    {
        ChunkAddress chunk{};
        LocalBlockCoord block{};
        friend constexpr auto operator<=>( const BlockAddress &, const BlockAddress & ) = default;
    };
    struct RelativeI64
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const RelativeI64 &, const RelativeI64 & ) = default;
    };

    inline constexpr GroupAddress originGroupAddress() { return {}; }
    inline constexpr ChunkAddress originChunkAddress() { return {}; }
    inline constexpr BlockAddress originBlockAddress() { return {}; }

    inline constexpr std::int64_t chunkVolume()
    {
        return BLOCKS_PER_CHUNK_EDGE * BLOCKS_PER_CHUNK_EDGE * BLOCKS_PER_CHUNK_EDGE;
    }
    inline constexpr std::int64_t groupVolume()
    {
        return CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE;
    }

    inline bool validLocal( std::int64_t value, std::int64_t radix ) noexcept
    {
        return value >= 0 && value < radix;
    }

    inline void requireCanonical( const GroupAddress &a )
    {
        if( !validLocal( a.region.x, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.y, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.z, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.group.x, GROUPS_PER_REGION_EDGE ) ||
            !validLocal( a.group.y, GROUPS_PER_REGION_EDGE ) ||
            !validLocal( a.group.z, GROUPS_PER_REGION_EDGE ) )
            throw std::invalid_argument( "non-canonical GroupAddress" );
    }
    inline void requireCanonical( const ChunkAddress &a )
    {
        requireCanonical( a.group );
        if( !validLocal( a.chunk.x, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( a.chunk.y, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( a.chunk.z, CHUNKS_PER_GROUP_EDGE ) )
            throw std::invalid_argument( "non-canonical ChunkAddress" );
    }
    inline void requireCanonical( const BlockAddress &a )
    {
        requireCanonical( a.chunk );
        if( !validLocal( a.block.x, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( a.block.y, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( a.block.z, BLOCKS_PER_CHUNK_EDGE ) )
            throw std::invalid_argument( "non-canonical BlockAddress" );
    }

    namespace detail
    {
        struct AxisAddress
        {
            std::int64_t sector = 0;
            std::int64_t region = 0;
            std::int64_t group = 0;
            std::int64_t chunk = 0;
            std::int64_t block = 0;
            friend constexpr auto operator<=>( const AxisAddress &, const AxisAddress & ) = default;
        };

        inline bool tryAddI64( std::int64_t a, std::int64_t b, std::int64_t &out ) noexcept
        {
            if( ( b > 0 && a > std::numeric_limits<std::int64_t>::max() - b ) ||
                ( b < 0 && a < std::numeric_limits<std::int64_t>::min() - b ) )
                return false;
            out = a + b;
            return true;
        }

        inline bool propagateDigit( std::int64_t &digit, std::int64_t radix,
                                    std::int64_t &carry ) noexcept
        {
            const std::int64_t quotient = floorDiv( carry, radix );
            const std::int64_t remainder = floorMod( carry, radix );
            const clonecraft_i128 sum = static_cast<clonecraft_i128>( digit ) + remainder;
            digit = static_cast<std::int64_t>( sum % radix );
            const std::int64_t extra = static_cast<std::int64_t>( sum / radix );
            return tryAddI64( quotient, extra, carry );
        }

        inline bool tryOffsetAxis( AxisAddress base, std::int64_t delta, AxisAddress &out ) noexcept
        {
            std::int64_t carry = floorDiv( delta, BLOCKS_PER_CHUNK_EDGE );
            const std::int64_t remainder = floorMod( delta, BLOCKS_PER_CHUNK_EDGE );
            const clonecraft_i128 blockSum = static_cast<clonecraft_i128>( base.block ) + remainder;
            base.block = static_cast<std::int64_t>( blockSum % BLOCKS_PER_CHUNK_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( blockSum / BLOCKS_PER_CHUNK_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.chunk, CHUNKS_PER_GROUP_EDGE, carry ) ||
                !propagateDigit( base.group, GROUPS_PER_REGION_EDGE, carry ) ||
                !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) )
                return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
        }

        inline bool tryOffsetChunkAxis( AxisAddress base, std::int64_t delta,
                                        AxisAddress &out ) noexcept
        {
            base.block = 0;
            std::int64_t carry = floorDiv( delta, CHUNKS_PER_GROUP_EDGE );
            const std::int64_t remainder = floorMod( delta, CHUNKS_PER_GROUP_EDGE );
            const clonecraft_i128 chunkSum = static_cast<clonecraft_i128>( base.chunk ) + remainder;
            base.chunk = static_cast<std::int64_t>( chunkSum % CHUNKS_PER_GROUP_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( chunkSum / CHUNKS_PER_GROUP_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.group, GROUPS_PER_REGION_EDGE, carry ) ||
                !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) )
                return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
        }

        inline bool tryOffsetGroupAxis( AxisAddress base, std::int64_t delta,
                                        AxisAddress &out ) noexcept
        {
            base.block = 0;
            base.chunk = 0;
            std::int64_t carry = floorDiv( delta, GROUPS_PER_REGION_EDGE );
            const std::int64_t remainder = floorMod( delta, GROUPS_PER_REGION_EDGE );
            const clonecraft_i128 groupSum = static_cast<clonecraft_i128>( base.group ) + remainder;
            base.group = static_cast<std::int64_t>( groupSum % GROUPS_PER_REGION_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( groupSum / GROUPS_PER_REGION_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) ) return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
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
            // Hot worldgen/mesh path: both points usually share a group.
            if( position.sector == origin.sector && position.region == origin.region &&
                position.group == origin.group )
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
                position.group == origin.group )
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
            if( position.sector == origin.sector && position.region == origin.region )
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
    { return { a.chunk.group.sector.x, a.chunk.group.region.x, a.chunk.group.group.x,
               a.chunk.chunk.x, a.block.x }; }
    inline detail::AxisAddress blockAxisY( const BlockAddress &a ) noexcept
    { return { a.chunk.group.sector.y, a.chunk.group.region.y, a.chunk.group.group.y,
               a.chunk.chunk.y, a.block.y }; }
    inline detail::AxisAddress blockAxisZ( const BlockAddress &a ) noexcept
    { return { a.chunk.group.sector.z, a.chunk.group.region.z, a.chunk.group.group.z,
               a.chunk.chunk.z, a.block.z }; }

    inline BlockAddress fromAxes( const detail::AxisAddress &x, const detail::AxisAddress &y,
                                  const detail::AxisAddress &z ) noexcept
    {
        return { { { { x.sector, y.sector, z.sector },
                       { x.region, y.region, z.region },
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
