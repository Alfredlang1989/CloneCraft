#include "TestHarness.h"
#include "camera/FreeCameraController.h"
#include <cmath>
TEST_CASE(camera_moves_in_dynamic_space){camera::FreeCameraController c;c.setPosition({15.75f,0.0f,0.0f});c.setOrientation(-1.5707963267948966,0.0);c.setSpeed(1.0);c.update(1.0,1.0,0.0,0.0);CHECK(std::abs(c.position().x-16.75f)<1e-5f);}
TEST_CASE(camera_pitch_is_clamped){camera::FreeCameraController c;c.setOrientation(0,100);CHECK(c.pitch()<=camera::FreeCameraController::MAX_PITCH);}
int main(){return test::runAll();}
