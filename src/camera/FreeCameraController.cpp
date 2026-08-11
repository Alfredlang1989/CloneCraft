#include "camera/FreeCameraController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace camera
{
    void FreeCameraController::setPosition( const spatial::dynamic::Position3f &position )
    {
        if( !std::isfinite( position.x ) || !std::isfinite( position.y ) || !std::isfinite( position.z ) )
            throw std::invalid_argument( "camera DynamicSpace position must be finite" );
        mPosition = position;
    }

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
        const double dx=(b.forward.x*forward+b.right.x*right+b.up.x*up)*dist;
        const double dy=(b.forward.y*forward+b.right.y*right+b.up.y*up)*dist;
        const double dz=(b.forward.z*forward+b.right.z*right+b.up.z*up)*dist;
        const double nx=static_cast<double>(mPosition.x)+dx;
        const double ny=static_cast<double>(mPosition.y)+dy;
        const double nz=static_cast<double>(mPosition.z)+dz;
        constexpr double maxFloat=static_cast<double>(std::numeric_limits<float>::max());
        if(!std::isfinite(nx)||!std::isfinite(ny)||!std::isfinite(nz)||
           std::abs(nx)>maxFloat||std::abs(ny)>maxFloat||std::abs(nz)>maxFloat)
            throw std::overflow_error("camera DynamicSpace position overflow");
        mPosition={static_cast<float>(nx),static_cast<float>(ny),static_cast<float>(nz)};
    }
} // namespace camera
