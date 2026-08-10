#include "world/coordinates/WorldPosition.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace world
{
    WorldPosition WorldPosition::fromBlockAddress( const BlockAddress &cell,
                                                   float fractionX,
                                                   float fractionY,
                                                   float fractionZ )
    {
        requireCanonical( cell );
        WorldPosition out;
        out.mCell = cell;
        out.translate( static_cast<double>( fractionX ), static_cast<double>( fractionY ),
                       static_cast<double>( fractionZ ) );
        return out;
    }

    WorldPosition WorldPosition::fromHierarchical( const GroupAddress &group,
                                                   const LocalChunkCoord &chunk,
                                                   const LocalBlockCoord &block,
                                                   float fractionX,
                                                   float fractionY,
                                                   float fractionZ )
    {
        return fromBlockAddress( { { group, chunk }, block }, fractionX, fractionY, fractionZ );
    }

    float WorldPosition::groupLocalX() const
    {
        return static_cast<float>( mCell.chunk.chunk.x * BLOCKS_PER_CHUNK_EDGE + mCell.block.x ) +
               mFractionX;
    }
    float WorldPosition::groupLocalY() const
    {
        return static_cast<float>( mCell.chunk.chunk.y * BLOCKS_PER_CHUNK_EDGE + mCell.block.y ) +
               mFractionY;
    }
    float WorldPosition::groupLocalZ() const
    {
        return static_cast<float>( mCell.chunk.chunk.z * BLOCKS_PER_CHUNK_EDGE + mCell.block.z ) +
               mFractionZ;
    }

    RelativeI64 WorldPosition::blockRelativeToGroup( const GroupAddress &origin ) const
    {
        const BlockAddress originBlock{ { origin, {} }, {} };
        RelativeI64 delta;
        constexpr std::int64_t kMaxBackendBlocks = 64 * BLOCKS_PER_GROUP_EDGE;
        if( !blockDeltaWithin( mCell, originBlock, kMaxBackendBlocks, delta ) )
            throw std::overflow_error( "WorldPosition backend origin is not local" );
        return delta;
    }

    RelativePosition3f WorldPosition::relativeToGroup( const GroupAddress &origin ) const
    {
        const RelativeI64 block = blockRelativeToGroup( origin );
        return { static_cast<float>( static_cast<double>( block.x ) + mFractionX ),
                 static_cast<float>( static_cast<double>( block.y ) + mFractionY ),
                 static_cast<float>( static_cast<double>( block.z ) + mFractionZ ) };
    }

    void WorldPosition::splitDelta( double currentFraction, double delta,
                                    std::int64_t &whole, float &fraction )
    {
        if( !std::isfinite( delta ) )
            throw std::invalid_argument( "WorldPosition translation must be finite" );
        const double continuous = currentFraction + delta;
        const double wholeD = std::floor( continuous );
        constexpr double MIN_I64 = -9223372036854775808.0;
        constexpr double MAX_I64_EXCLUSIVE = 9223372036854775808.0;
        if( wholeD < MIN_I64 || wholeD >= MAX_I64_EXCLUSIVE )
            throw std::overflow_error( "one WorldPosition translation step exceeds int64" );
        whole = static_cast<std::int64_t>( wholeD );
        double rem = continuous - wholeD;
        if( rem < 0.0 ) rem = 0.0;
        if( rem >= 1.0 )
        {
            rem = 0.0;
            if( whole == std::numeric_limits<std::int64_t>::max() )
                throw std::overflow_error( "WorldPosition fraction carry overflow" );
            ++whole;
        }
        fraction = static_cast<float>( rem );
        if( fraction >= 1.0f )
        {
            fraction = 0.0f;
            if( whole == std::numeric_limits<std::int64_t>::max() )
                throw std::overflow_error( "WorldPosition fraction carry overflow" );
            ++whole;
        }
    }

    void WorldPosition::translate( double dx, double dy, double dz )
    {
        std::int64_t ix = 0, iy = 0, iz = 0;
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;
        splitDelta( static_cast<double>( mFractionX ), dx, ix, fx );
        splitDelta( static_cast<double>( mFractionY ), dy, iy, fy );
        splitDelta( static_cast<double>( mFractionZ ), dz, iz, fz );
        BlockAddress next;
        if( !tryOffsetBlock( mCell, ix, iy, iz, next ) )
            throw std::overflow_error( "WorldPosition crossed the representable SectorCoord range" );
        mCell = next;
        mFractionX = fx;
        mFractionY = fy;
        mFractionZ = fz;
    }
} // namespace world
