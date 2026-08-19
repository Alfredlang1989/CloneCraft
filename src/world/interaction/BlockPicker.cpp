#include "world/interaction/BlockPicker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace world::interaction
{
    namespace
    {
        constexpr double EPSILON = 1.0e-12;

        bool sameCrossing( double value, double minimum )
        {
            // Non-finite axes (no movement on that axis) never cross
            // (M02-C: an untouched axis must not be reported as a face).
            if( !std::isfinite( value ) || !std::isfinite( minimum ) )
                return false;
            return std::fabs( value - minimum ) <= EPSILON *
                   std::max( { 1.0, std::fabs( value ), std::fabs( minimum ) } );
        }

        struct AxisTraversal
        {
            int step = 0;
            double tMax = std::numeric_limits<double>::infinity();
            double tDelta = std::numeric_limits<double>::infinity();
        };

        AxisTraversal makeAxisTraversal( double fraction, double direction )
        {
            if( direction > EPSILON )
                return { 1, ( 1.0 - fraction ) / direction, 1.0 / direction };
            if( direction < -EPSILON )
                return { -1, fraction / -direction, 1.0 / -direction };
            return {};
        }
    } // namespace

    std::int64_t faceNormalX( BlockFace face )
    {
        // Outer normal of the hit face: points back at the ray origin.
        switch( face )
        {
            case BlockFace::NegativeX: return -1;
            case BlockFace::PositiveX: return 1;
            default: return 0;
        }
    }

    std::int64_t faceNormalY( BlockFace face )
    {
        switch( face )
        {
            case BlockFace::NegativeY: return -1;
            case BlockFace::PositiveY: return 1;
            default: return 0;
        }
    }

    std::int64_t faceNormalZ( BlockFace face )
    {
        switch( face )
        {
            case BlockFace::NegativeZ: return -1;
            case BlockFace::PositiveZ: return 1;
            default: return 0;
        }
    }

    std::optional<BlockPickResult> pickBlock( const ChunkManager &chunks,
                                               const WorldPosition &origin,
                                               double dirX, double dirY, double dirZ,
                                               double maxDistance )
    {
        if( !std::isfinite( dirX ) || !std::isfinite( dirY ) || !std::isfinite( dirZ ) ||
            !std::isfinite( maxDistance ) || maxDistance <= 0.0 )
            return std::nullopt;

        const double length = std::sqrt( dirX * dirX + dirY * dirY + dirZ * dirZ );
        if( length <= EPSILON )
            return std::nullopt;
        dirX /= length;
        dirY /= length;
        dirZ /= length;

        BlockAddress cell = origin.blockAddress();
        if( const auto voxel = chunks.tryBlockAt( cell ); voxel && *voxel != 0u )
            return BlockPickResult{ cell, *voxel, 0.0, BlockFace::None, std::nullopt };

        const AxisTraversal xAxis = makeAxisTraversal( origin.fractionX(), dirX );
        const AxisTraversal yAxis = makeAxisTraversal( origin.fractionY(), dirY );
        const AxisTraversal zAxis = makeAxisTraversal( origin.fractionZ(), dirZ );

        const int stepX = xAxis.step;
        const int stepY = yAxis.step;
        const int stepZ = zAxis.step;
        double tMaxX = xAxis.tMax;
        double tMaxY = yAxis.tMax;
        double tMaxZ = zAxis.tMax;
        const double tDeltaX = xAxis.tDelta;
        const double tDeltaY = yAxis.tDelta;
        const double tDeltaZ = zAxis.tDelta;

        for( ;; )
        {
            const double nextDistance = std::min( { tMaxX, tMaxY, tMaxZ } );
            if( !std::isfinite( nextDistance ) || nextDistance > maxDistance )
                break;

            const bool crossX = sameCrossing( tMaxX, nextDistance );
            const bool crossY = sameCrossing( tMaxY, nextDistance );
            const bool crossZ = sameCrossing( tMaxZ, nextDistance );

            // Round 6 (MAJOR 1): exact edge/corner ties must NOT jump
            // diagonally. Every axis that crosses at `nextDistance` is
            // traversed SEQUENTIALLY in deterministic order X, then Y, then
            // Z - one canonical step, one hit check, one face per step.
            // All tie steps share the same geometric distance.
            for( int axis = 0; axis < 3; ++axis )
            {
                const bool crosses = axis == 0 ? crossX : ( axis == 1 ? crossY : crossZ );
                if( !crosses )
                    continue;
                const int step = axis == 0 ? stepX : ( axis == 1 ? stepY : stepZ );
                const std::int64_t dx = axis == 0 ? step : 0;
                const std::int64_t dy = axis == 1 ? step : 0;
                const std::int64_t dz = axis == 2 ? step : 0;

                BlockAddress next{};
                if( !tryOffsetBlock( cell, dx, dy, dz, next ) )
                {
                    // Round 7: a canonical step that cannot be represented
                    // (hierarchical overflow at the sector edge) TERMINATES
                    // the whole traversal with nullopt - no spin, no wrap,
                    // no clamp, no invented neighbour. The sequential
                    // X->Y->Z tie behavior is unaffected in the representable
                    // domain.
                    return std::nullopt;
                }
                cell = next;

                if( axis == 0 ) tMaxX += tDeltaX;
                if( axis == 1 ) tMaxY += tDeltaY;
                if( axis == 2 ) tMaxZ += tDeltaZ;

                if( const auto voxel = chunks.tryBlockAt( cell ); voxel && *voxel != 0u )
                {
                    // The face belongs to THIS concrete axis step.
                    const BlockFace face = axis == 0 ? ( step > 0 ? BlockFace::NegativeX
                                                                  : BlockFace::PositiveX )
                                               : axis == 1 ? ( step > 0 ? BlockFace::NegativeY
                                                                          : BlockFace::PositiveY )
                                                           : ( step > 0 ? BlockFace::NegativeZ
                                                                        : BlockFace::PositiveZ );
                    BlockAddress adjacent{};
                    const bool hasAdjacent = tryOffsetBlock(
                        cell, faceNormalX( face ), faceNormalY( face ), faceNormalZ( face ),
                        adjacent );
                    return BlockPickResult{ cell, *voxel, nextDistance, face,
                                            hasAdjacent
                                                ? std::optional<BlockAddress>{ adjacent }
                                                : std::nullopt };
                }
            }
        }

        return std::nullopt;
    }
} // namespace world::interaction
