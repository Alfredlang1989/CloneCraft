#include "spatial/dynamic/DynamicSpace.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace spatial::dynamic
{
    DynamicSpace::DynamicSpace( std::int64_t edgeBlocks ) : mEdgeBlocks( edgeBlocks )
    {
        if( edgeBlocks <= 0 || ( edgeBlocks & 1 ) != 0 )
            throw std::invalid_argument( "DynamicSpace edge must be a positive even block count" );

        // Rebase offsets must remain exactly representable as float. This keeps
        // a global rebase from injecting rounding into every active body.
        constexpr std::int64_t kMaxExactlyRepresentableInteger = INT64_C( 1 ) << 24;
        if( edgeBlocks > kMaxExactlyRepresentableInteger )
            throw std::invalid_argument( "DynamicSpace edge exceeds exact float integer range" );
    }

    std::int64_t DynamicSpace::axisRebaseDelta( float coordinate ) const
    {
        if( !std::isfinite( coordinate ) )
            throw std::invalid_argument( "DynamicSpace position must be finite" );

        const double c = static_cast<double>( coordinate );
        const double edge = static_cast<double>( mEdgeBlocks );
        const double half = edge * 0.5;
        if( c >= -half && c < half ) return 0;

        // Choose an integer number of full DynamicSpace edges which maps the
        // coordinate back into [-half,+half). This also handles teleports.
        const double stepsD = std::floor( ( c + half ) / edge );
        if( stepsD < static_cast<double>( std::numeric_limits<std::int64_t>::min() / mEdgeBlocks ) ||
            stepsD > static_cast<double>( std::numeric_limits<std::int64_t>::max() / mEdgeBlocks ) )
            throw std::overflow_error( "DynamicSpace rebase delta overflow" );
        return static_cast<std::int64_t>( stepsD ) * mEdgeBlocks;
    }

    RebaseDelta DynamicSpace::rebaseDeltaFor( const Position3f &position ) const
    {
        return { axisRebaseDelta( position.x ), axisRebaseDelta( position.y ),
                 axisRebaseDelta( position.z ) };
    }

    void DynamicSpace::applyRebase( Position3f &position, const RebaseDelta &delta ) const
    {
        if( delta.x % mEdgeBlocks != 0 || delta.y % mEdgeBlocks != 0 ||
            delta.z % mEdgeBlocks != 0 )
            throw std::invalid_argument( "DynamicSpace rebase delta must align to its edge" );

        position.x -= static_cast<float>( delta.x );
        position.y -= static_cast<float>( delta.y );
        position.z -= static_cast<float>( delta.z );

        const double half = halfEdgeBlocks();
        const auto inRange = [half]( float value ) {
            return std::isfinite( value ) && static_cast<double>( value ) >= -half &&
                   static_cast<double>( value ) < half;
        };
        if( !inRange( position.x ) || !inRange( position.y ) || !inRange( position.z ) )
            throw std::logic_error( "DynamicSpace rebase did not restore local range" );
    }
} // namespace spatial::dynamic
