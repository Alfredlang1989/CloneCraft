#include "TestHarness.h"
#include "world/interaction/BlockPicker.h"

#include <cmath>

TEST_CASE(block_picker_follows_view_ray_and_group_carries)
{
    world::ChunkManager chunks;
    const auto originCell = world::fromOriginOffset( 0, 0, 0 );
    const auto target = world::fromOriginOffset( 0, 0, -4 );
    chunks.setBlock( target, 7u );

    const world::WorldPosition camera =
        world::WorldPosition::fromBlockAddress( originCell, 0.5f, 0.5f, 0.5f );
    const auto hit = world::interaction::pickBlock( chunks, camera, 0.0, 0.0, -1.0, 8.0 );
    CHECK( hit.has_value() );
    if( !hit )
        return;
    CHECK( hit->block == target );
    CHECK( hit->runtimeId == 7u );
    CHECK( std::fabs( hit->distance - 3.5 ) < 1.0e-9 );

    const auto tooShort = world::interaction::pickBlock( chunks, camera, 0.0, 0.0, -1.0, 3.0 );
    CHECK( !tooShort.has_value() );

    // Exercise canonical carries across chunk/group boundaries without ever
    // converting the hierarchical address to a giant floating-point world XYZ.
    const auto boundaryOrigin = world::fromOriginOffset(
        world::BLOCKS_PER_GROUP_EDGE - 1, 0, 0 );
    const auto beyondBoundary = world::offsetBlock( boundaryOrigin, 2, 0, 0 );
    chunks.setBlock( beyondBoundary, 9u );
    const auto boundaryCamera = world::WorldPosition::fromBlockAddress(
        boundaryOrigin, 0.75f, 0.5f, 0.5f );
    const auto boundaryHit = world::interaction::pickBlock(
        chunks, boundaryCamera, 1.0, 0.0, 0.0, 4.0 );
    CHECK( boundaryHit.has_value() );
    if( !boundaryHit )
        return;
    CHECK( boundaryHit->block == beyondBoundary );
    CHECK( boundaryHit->runtimeId == 9u );

}

int main() { return test::runAll(); }
