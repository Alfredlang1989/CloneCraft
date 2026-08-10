#include "TestHarness.h"
#include "camera/FreeCameraController.h"
#include "world/coordinates/Coords.h"
#include <cmath>

TEST_CASE(camera_moves_hierarchical_position)
{
    camera::FreeCameraController c;
    auto start=world::fromOriginOffset(15,0,0);
    c.setPosition(world::WorldPosition::fromBlockAddress(start,0.75f,0,0));
    c.setOrientation(-1.5707963267948966,0.0);
    c.setSpeed(1.0);
    c.update(1.0,1.0,0.0,0.0);
    world::RelativeI64 d{};
    CHECK(world::blockDeltaWithin(c.position().blockAddress(),start,2,d));
    CHECK_EQ(d.x,1);
}

TEST_CASE(camera_pitch_is_clamped)
{
    camera::FreeCameraController c; c.setOrientation(0,100); CHECK(c.pitch()<=camera::FreeCameraController::MAX_PITCH);
}
int main(){return test::runAll();}
