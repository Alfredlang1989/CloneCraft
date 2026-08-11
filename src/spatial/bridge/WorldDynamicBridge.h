#pragma once

#include "spatial/dynamic/DynamicSpace.h"
#include "world/coordinates/WorldPosition.h"

#include <cstdint>

namespace spatial::bridge
{
    /**
     * The single translation boundary between the exact hierarchical block
     * world and the local float DynamicSpace.
     *
     * World/chunk/worldgen code never depends on DynamicSpace. Camera, future
     * NPCs and Jolt never need Sector/Region/Section arithmetic themselves.
     */
    class WorldDynamicBridge
    {
    public:
        explicit WorldDynamicBridge( world::BlockAddress anchor = world::originBlockAddress() );

        const world::BlockAddress &anchor() const noexcept { return mAnchor; }
        void setAnchor( const world::BlockAddress &anchor );

        world::WorldPosition toWorld( const dynamic::Position3f &local ) const;
        dynamic::Position3f toLocal( const world::WorldPosition &worldPosition,
                                     std::int64_t maxAbsBlocks ) const;

        void shiftAnchor( const dynamic::RebaseDelta &delta );

    private:
        world::BlockAddress mAnchor{};
    };
} // namespace spatial::bridge
