#include "TestHarness.h"
#include "app/DebugCoordinateCompat.h"
#include "world/coordinates/Coords.h"

TEST_CASE(debug_flat_coordinate_matches_origin_and_negative_neighbor)
{
    const world::BlockAddress origin = world::originBlockAddress();
    CHECK( app::debug_compat::flattenedCoordinate( origin, app::debug_compat::Axis::X ) == "0" );
    CHECK( app::debug_compat::flattenedCoordinate( origin, app::debug_compat::Axis::Y ) == "0" );
    CHECK( app::debug_compat::flattenedCoordinate( origin, app::debug_compat::Axis::Z ) == "0" );

    world::BlockAddress minusOne{};
    CHECK( world::tryOffsetBlock( origin, -1, -1, -1, minusOne ) );
    CHECK( app::debug_compat::flattenedCoordinate( minusOne, app::debug_compat::Axis::X ) == "-1" );
    CHECK( app::debug_compat::flattenedCoordinate( minusOne, app::debug_compat::Axis::Y ) == "-1" );
    CHECK( app::debug_compat::flattenedCoordinate( minusOne, app::debug_compat::Axis::Z ) == "-1" );
}

TEST_CASE(debug_flat_coordinate_preserves_large_sector_values)
{
    world::BlockAddress address{};
    address.chunk.group.sector.x = 8'000'000'000'000'000'000LL;

    const std::string x =
        app::debug_compat::flattenedCoordinate( address, app::debug_compat::Axis::X );
    CHECK( x.size() > 19 );
    CHECK( x.rfind( "524288", 0 ) == 0 );
}

int main() { return test::runAll(); }
