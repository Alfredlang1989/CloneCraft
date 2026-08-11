#include "spatial/bridge/WorldDynamicBridge.h"

#include <stdexcept>

namespace spatial::bridge
{
    WorldDynamicBridge::WorldDynamicBridge( world::BlockAddress anchor ) : mAnchor( anchor )
    {
        world::requireCanonical( mAnchor );
    }

    void WorldDynamicBridge::setAnchor( const world::BlockAddress &anchor )
    {
        world::requireCanonical( anchor );
        mAnchor = anchor;
    }

    world::WorldPosition WorldDynamicBridge::toWorld( const dynamic::Position3f &local ) const
    {
        world::WorldPosition result = world::WorldPosition::fromBlockAddress( mAnchor );
        result.translate( static_cast<double>( local.x ), static_cast<double>( local.y ),
                          static_cast<double>( local.z ) );
        return result;
    }

    dynamic::Position3f WorldDynamicBridge::toLocal( const world::WorldPosition &worldPosition,
                                                     std::int64_t maxAbsBlocks ) const
    {
        world::RelativeI64 delta{};
        if( maxAbsBlocks < 0 ||
            !world::blockDeltaWithin( worldPosition.blockAddress(), mAnchor, maxAbsBlocks, delta ) )
            throw std::overflow_error( "world position lies outside requested DynamicSpace bridge range" );

        return {
            static_cast<float>( static_cast<double>( delta.x ) + worldPosition.fractionX() ),
            static_cast<float>( static_cast<double>( delta.y ) + worldPosition.fractionY() ),
            static_cast<float>( static_cast<double>( delta.z ) + worldPosition.fractionZ() )
        };
    }

    void WorldDynamicBridge::shiftAnchor( const dynamic::RebaseDelta &delta )
    {
        world::BlockAddress shifted{};
        if( !world::tryOffsetBlock( mAnchor, delta.x, delta.y, delta.z, shifted ) )
            throw std::overflow_error( "DynamicSpace anchor crossed SectorCoord range" );
        mAnchor = shifted;
    }
} // namespace spatial::bridge
