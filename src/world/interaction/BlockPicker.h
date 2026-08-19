#pragma once

#include "world/chunk/ChunkManager.h"
#include "world/coordinates/WorldPosition.h"

#include <cstdint>
#include <optional>

namespace world::interaction
{
    /** The cube face a ray entered the hit block through (M02-C, #18). */
    enum class BlockFace : std::uint8_t
    {
        None,
        NegativeX, // entered through the -X face (moving +X)
        PositiveX, // entered through the +X face (moving -X)
        NegativeY,
        PositiveY,
        NegativeZ,
        PositiveZ
    };

    /**
     * Result of a precision-safe voxel DDA pick (extended in M02-C):
     * the hit block, its runtime id, the distance along the ray, the exact
     * entered face and the canonical adjacency ("placement") position on
     * the camera side of that face (the cell the player looks at, never the
     * far side). DDA advances through hierarchical BlockAddress
     * carries, so huge sector coordinates are never flattened to double.
     */
    struct BlockPickResult
    {
        BlockAddress block{};
        std::uint16_t runtimeId = 0;
        double distance = 0.0;
        BlockFace face = BlockFace::None;
        /** Position on the other side of the hit face (placement).
         *  Absent when the pick never crossed a face (origin hit). */
        std::optional<BlockAddress> adjacent;
    };

    /** Unit face normal (M02 review): the OUTER normal of the hit face -
     *  the direction away from the hit block, facing the ray origin.
     *  adjacent = hit cell + normal is the cell the player looks at
     *  (placement goes on the player's side, never behind the block).
     *  Returns {0,0,0} for BlockFace::None. */
    std::int64_t faceNormalX( BlockFace face );
    std::int64_t faceNormalY( BlockFace face );
    std::int64_t faceNormalZ( BlockFace face );

    /**
     * Precision-safe voxel DDA. Only the short ray from the camera is floating
     * point; every crossed cell is advanced through hierarchical BlockAddress
     * carries, so huge sector coordinates are never flattened to double.
     *
     * Deterministic tie-breaking (M02-C): when the ray crosses two or three
     * planes at the exact same distance (corner/edge), the faces are
     * resolved in X, then Y, then Z order.
     */
    std::optional<BlockPickResult> pickBlock( const ChunkManager &chunks,
                                                const WorldPosition &origin,
                                                double dirX, double dirY, double dirZ,
                                                double maxDistance );
} // namespace world::interaction
