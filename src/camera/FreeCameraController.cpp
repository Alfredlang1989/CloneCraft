#include "camera/FreeCameraController.h"
#include <algorithm>
#include <cmath>

namespace camera
{
    void FreeCameraController::setPosition( const world::WorldPosition &position ) { mPosition = position; }
    void FreeCameraController::setOrientation( double yawRadians, double pitchRadians )
    { mYaw=yawRadians; mPitch=std::clamp(pitchRadians,-MAX_PITCH,MAX_PITCH); }
    void FreeCameraController::rotate( double yawDeltaRadians, double pitchDeltaRadians )
    { mYaw+=yawDeltaRadians; mPitch=std::clamp(mPitch+pitchDeltaRadians,-MAX_PITCH,MAX_PITCH); }

    MovementBasis FreeCameraController::basis() const
    {
        const double cp=std::cos(mPitch), sp=std::sin(mPitch), cy=std::cos(mYaw), sy=std::sin(mYaw);
        MovementBasis b;
        b.forward={-sy*cp,sp,-cy*cp};
        b.right={cy,0.0,-sy};
        b.up={b.right.y*b.forward.z-b.right.z*b.forward.y,
              b.right.z*b.forward.x-b.right.x*b.forward.z,
              b.right.x*b.forward.y-b.right.y*b.forward.x};
        return b;
    }

    void FreeCameraController::update( double dt, double forward, double right, double up )
    {
        const MovementBasis b=basis();
        const double dist=dt*mSpeed;
        mPosition.translate((b.forward.x*forward+b.right.x*right+b.up.x*up)*dist,
                            (b.forward.y*forward+b.right.y*right+b.up.y*up)*dist,
                            (b.forward.z*forward+b.right.z*right+b.up.z*up)*dist);
    }
} // namespace camera
