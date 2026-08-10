#pragma once

#include "world/coordinates/WorldPosition.h"

namespace camera
{
    struct Vec3d { double x=0.0, y=0.0, z=0.0; };
    struct MovementBasis { Vec3d forward{}, right{}, up{}; };

    /** Precision-safe free-fly camera over a hierarchical WorldPosition. */
    class FreeCameraController
    {
    public:
        static constexpr double MAX_PITCH = 1.552;
        void setPosition( const world::WorldPosition &position );
        const world::WorldPosition &position() const { return mPosition; }
        void setOrientation( double yawRadians, double pitchRadians );
        void rotate( double yawDeltaRadians, double pitchDeltaRadians );
        void update( double dt, double forward, double right, double up );
        double yaw() const { return mYaw; }
        double pitch() const { return mPitch; }
        MovementBasis basis() const;
        double speed() const { return mSpeed; }
        void setSpeed( double blocksPerSecond ) { mSpeed = blocksPerSecond; }
    private:
        world::WorldPosition mPosition{};
        double mYaw = 0.0;
        double mPitch = 0.0;
        double mSpeed = 30.0;
    };
} // namespace camera
