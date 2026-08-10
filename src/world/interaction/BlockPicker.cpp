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
            return BlockPickResult{ cell, *voxel, 0.0 };

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
            const std::int64_t dx = crossX ? stepX : 0;
            const std::int64_t dy = crossY ? stepY : 0;
            const std::int64_t dz = crossZ ? stepZ : 0;

            BlockAddress next{};
            if( !tryOffsetBlock( cell, dx, dy, dz, next ) )
                break;
            cell = next;

            if( crossX ) tMaxX += tDeltaX;
            if( crossY ) tMaxY += tDeltaY;
            if( crossZ ) tMaxZ += tDeltaZ;

            if( const auto voxel = chunks.tryBlockAt( cell ); voxel && *voxel != 0u )
                return BlockPickResult{ cell, *voxel, nextDistance };
        }

        return std::nullopt;
    }
} // namespace world::interaction
