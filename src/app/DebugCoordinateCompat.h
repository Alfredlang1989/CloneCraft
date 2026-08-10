#pragma once

#include "world/coordinates/Coords.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace app::debug_compat
{
    enum class Axis
    {
        X,
        Y,
        Z
    };

    /**
     * Temporary v16 comparison helper only.
     *
     * Production systems must keep using the hierarchical address. This helper
     * reconstructs the old flat block-axis number solely for F5 diagnostics.
     */
    inline world::clonecraft_i128 flattenedAxis( const world::BlockAddress &address, Axis axis )
    {
        std::int64_t sector = 0;
        std::int64_t region = 0;
        std::int64_t group = 0;
        std::int64_t chunk = 0;
        std::int64_t block = 0;

        switch( axis )
        {
        case Axis::X:
            sector = address.chunk.group.sector.x;
            region = address.chunk.group.region.x;
            group = address.chunk.group.group.x;
            chunk = address.chunk.chunk.x;
            block = address.block.x;
            break;
        case Axis::Y:
            sector = address.chunk.group.sector.y;
            region = address.chunk.group.region.y;
            group = address.chunk.group.group.y;
            chunk = address.chunk.chunk.y;
            block = address.block.y;
            break;
        case Axis::Z:
            sector = address.chunk.group.sector.z;
            region = address.chunk.group.region.z;
            group = address.chunk.group.group.z;
            chunk = address.chunk.chunk.z;
            block = address.block.z;
            break;
        }

        world::clonecraft_i128 value = sector;
        value = value * world::REGIONS_PER_SECTOR_EDGE + region;
        value = value * world::GROUPS_PER_REGION_EDGE + group;
        value = value * world::CHUNKS_PER_GROUP_EDGE + chunk;
        value = value * world::BLOCKS_PER_CHUNK_EDGE + block;
        return value;
    }

    inline std::string decimal( world::clonecraft_i128 value )
    {
        if( value == 0 )
            return "0";

        const bool negative = value < 0;
        if( negative )
            value = -value;

        std::string text;
        while( value != 0 )
        {
            const int digit = static_cast<int>( value % 10 );
            text.push_back( static_cast<char>( '0' + digit ) );
            value /= 10;
        }
        if( negative )
            text.push_back( '-' );
        std::reverse( text.begin(), text.end() );
        return text;
    }

    inline std::string flattenedCoordinate( const world::BlockAddress &address, Axis axis )
    {
        return decimal( flattenedAxis( address, axis ) );
    }
} // namespace app::debug_compat
