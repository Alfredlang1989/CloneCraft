#pragma once

#include "world/chunk/Sidecar.h"

#include <cstdint>
#include <optional>

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

    /** Numeric value of an orientation for the "core:orientation" sidecar. */
    inline constexpr std::uint32_t blockOrientationValue( BlockOrientation orientation )
    {
        return static_cast<std::uint32_t>( orientation );
    }

    /** Decodes a stored sidecar value; nullopt for invalid encodings. */
    inline std::optional<BlockOrientation> blockOrientationFromValue( std::uint32_t value )
    {
        if( value > static_cast<std::uint32_t>( BlockOrientation::West ) )
            return std::nullopt;
        return static_cast<BlockOrientation>( value );
    }

    /** Sparse per-chunk orientation storage; default orientation is Up.
     *  Test/typed face of the generic sidecar framework: since M05 the chunk
     *  itself stores PropertyValue sidecars, this typed wrapper is used by
     *  tests and the ChunkManager orientation shim. */
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
