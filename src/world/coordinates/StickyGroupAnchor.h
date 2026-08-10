#pragma once

#include "world/coordinates/WorldPosition.h"

namespace world
{
    struct RelativePosition3d
    {
        double x = 0.0, y = 0.0, z = 0.0;
        friend constexpr auto operator<=>( const RelativePosition3d &, const RelativePosition3d & ) = default;
    };

    class StickyGroupAnchor
    {
    public:
        static constexpr double kDefaultThreshold = 0.8;
        explicit StickyGroupAnchor( double threshold = kDefaultThreshold );

        void teleportTo( const WorldPosition &position );
        bool update( const WorldPosition &position );

        GroupAddress owner() const { return mOwner; }
        RelativePosition3d localPosition() const { return { mLocalX, mLocalY, mLocalZ }; }

    private:
        GroupAddress mOwner{};
        double mLocalX = 0.0;
        double mLocalY = 0.0;
        double mLocalZ = 0.0;
        double mThreshold;
    };
} // namespace world
