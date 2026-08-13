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
#error "Clonecraft hierarchical phase-2 coordinates require compiler support for 128-bit integers"
#endif
#ifndef CLONECRAFT_CHUNK_EDGE
#define CLONECRAFT_CHUNK_EDGE 16
#endif
#ifndef CLONECRAFT_GROUP_EDGE
#define CLONECRAFT_GROUP_EDGE 16
#endif
#ifndef CLONECRAFT_SECTION_EDGE
#define CLONECRAFT_SECTION_EDGE 256
#endif
#ifndef CLONECRAFT_REGION_EDGE
#define CLONECRAFT_REGION_EDGE 9000000000000000000LL
#endif
#ifndef CLONECRAFT_SECTOR_EDGE
#define CLONECRAFT_SECTOR_EDGE 9000000000000000000LL
#endif

    // Physical storage radices. These remain small because a Chunk and ChunkGroup
    // are materialized containers.
    inline constexpr std::int64_t BLOCKS_PER_CHUNK_EDGE = CLONECRAFT_CHUNK_EDGE;
    inline constexpr std::int64_t CHUNKS_PER_GROUP_EDGE = CLONECRAFT_GROUP_EDGE;

    // Phase 2 logical address radices. A Section is the locality tier between
    // physical ChunkGroups and the astronomically large logical super-levels.
    // No Section/Region/Sector container is materialized from these radices.
    inline constexpr std::int64_t GROUPS_PER_SECTION_EDGE = CLONECRAFT_SECTION_EDGE;
    inline constexpr std::int64_t SECTIONS_PER_REGION_EDGE = CLONECRAFT_REGION_EDGE;
    inline constexpr std::int64_t REGIONS_PER_SECTOR_EDGE = CLONECRAFT_SECTOR_EDGE;

    inline constexpr std::int64_t BLOCKS_PER_GROUP_EDGE =
        BLOCKS_PER_CHUNK_EDGE * CHUNKS_PER_GROUP_EDGE;

    static_assert( BLOCKS_PER_CHUNK_EDGE > 0 );
    static_assert( CHUNKS_PER_GROUP_EDGE > 0 );
    static_assert( GROUPS_PER_SECTION_EDGE > 0 );
    static_assert( SECTIONS_PER_REGION_EDGE > 0 );
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
    struct LocalSectionCoord
    {
        std::int64_t x = 0, y = 0, z = 0;
        friend constexpr auto operator<=>( const LocalSectionCoord &, const LocalSectionCoord & ) = default;
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
        LocalSectionCoord section{};
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

    /** Exact address tier used by debug/navigation teleports. */
    enum class HierarchyLevel : std::uint8_t
    {
        Block,
        Chunk,
        Group,
        Section,
        Region,
        Sector
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
