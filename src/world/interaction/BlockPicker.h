#pragma once

#include "world/chunk/ChunkManager.h"
#include "world/coordinates/WorldPosition.h"

#include <cstdint>
#include <optional>

namespace world::interaction
{
    struct BlockPickResult
    {
        BlockAddress block{};
        std::uint16_t runtimeId = 0;
        double distance = 0.0;
    };

    /**
     * Precision-safe voxel DDA. Only the short ray from the camera is floating
     * point; every crossed cell is advanced through hierarchical BlockAddress
     * carries, so huge sector coordinates are never flattened to double.
     */
    std::optional<BlockPickResult> pickBlock( const ChunkManager &chunks,
                                               const WorldPosition &origin,
                                               double dirX, double dirY, double dirZ,
                                               double maxDistance );
} // namespace world::interaction
