#include "world/coordinates/StickyGroupAnchor.h"

#include <cmath>
#include <stdexcept>

namespace world
{
    StickyGroupAnchor::StickyGroupAnchor( double threshold ) : mThreshold( threshold )
    {
        if( threshold <= 0.5 || threshold >= 1.0 )
            throw std::invalid_argument( "StickyGroupAnchor threshold must be in (0.5, 1.0)" );
    }

    void StickyGroupAnchor::teleportTo( const WorldPosition &position )
    {
        const double size = static_cast<double>( BLOCKS_PER_GROUP_EDGE );
        const double half = size * 0.5;
        mOwner = position.group();
        mLocalX = position.groupLocalX();
        mLocalY = position.groupLocalY();
        mLocalZ = position.groupLocalZ();

        const auto center = [&]( int axis, double &local ) {
            if( local <= half ) return;
            if( axis == 0 ) mOwner = offsetGroup( mOwner, 1, 0, 0 );
            if( axis == 1 ) mOwner = offsetGroup( mOwner, 0, 1, 0 );
            if( axis == 2 ) mOwner = offsetGroup( mOwner, 0, 0, 1 );
            local -= size;
        };
        center( 0, mLocalX );
        center( 1, mLocalY );
        center( 2, mLocalZ );
    }

    bool StickyGroupAnchor::update( const WorldPosition &position )
    {
        const double size = static_cast<double>( BLOCKS_PER_GROUP_EDGE );
        const double limit = mThreshold * size;
        RelativeI64 delta{};
        bool changed = false;

        if( !groupDeltaWithin( position.group(), mOwner, 2, delta ) )
        {
            mOwner = position.group();
            mLocalX = position.groupLocalX();
            mLocalY = position.groupLocalY();
            mLocalZ = position.groupLocalZ();
            return true;
        }

        mLocalX = static_cast<double>( delta.x ) * size + position.groupLocalX();
        mLocalY = static_cast<double>( delta.y ) * size + position.groupLocalY();
        mLocalZ = static_cast<double>( delta.z ) * size + position.groupLocalZ();

        auto adjust = [&]( int axis, double &local ) {
            if( local > limit )
            {
                const std::int64_t steps = static_cast<std::int64_t>(
                    std::floor( ( local - limit ) / size ) ) + 1;
                if( axis == 0 ) mOwner = offsetGroup( mOwner, steps, 0, 0 );
                if( axis == 1 ) mOwner = offsetGroup( mOwner, 0, steps, 0 );
                if( axis == 2 ) mOwner = offsetGroup( mOwner, 0, 0, steps );
                local -= static_cast<double>( steps ) * size;
                changed = true;
            }
            else if( local < -limit )
            {
                const std::int64_t steps = static_cast<std::int64_t>(
                    std::floor( ( -local - limit ) / size ) ) + 1;
                if( axis == 0 ) mOwner = offsetGroup( mOwner, -steps, 0, 0 );
                if( axis == 1 ) mOwner = offsetGroup( mOwner, 0, -steps, 0 );
                if( axis == 2 ) mOwner = offsetGroup( mOwner, 0, 0, -steps );
                local += static_cast<double>( steps ) * size;
                changed = true;
            }
        };
        adjust( 0, mLocalX );
        adjust( 1, mLocalY );
        adjust( 2, mLocalZ );
        return changed;
    }
} // namespace world
