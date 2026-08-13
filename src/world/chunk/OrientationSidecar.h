#pragma once

#include "world/chunk/Sidecar.h"

#include <cstdint>

namespace world
{
    /**
     * Physical facing of a block. The default orientation is Up (encoded 0),
     * matching the SidecarDef "core:orientation" default in MODS/Default
     * (3-bit compact encoding per issue #3, section 5).
     */
    enum class BlockOrientation : std::uint8_t
    {
        Up = 0,
        Down = 1,
        North = 2,
        South = 3,
        East = 4,
        West = 5
    };

    /** Sparse per-chunk orientation storage; default orientation is Up. */
    class OrientationSidecar : public Sidecar<BlockOrientation>
    {
    public:
        /** @param capacity number of valid local indices (Chunk::VOLUME). */
        explicit OrientationSidecar( std::uint32_t capacity = 0u ) :
            Sidecar<BlockOrientation>( BlockOrientation::Up, capacity )
        {
        }
    };
} // namespace world
