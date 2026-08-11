#include "TestHarness.h"
#include "spatial/bridge/WorldDynamicBridge.h"
#include "spatial/dynamic/DynamicSpace.h"
#include "world/coordinates/Coords.h"

#include <cmath>
#include <limits>

TEST_CASE(dynamic_space_defaults_to_64k_and_rebases_only_at_half_edge)
{
    spatial::dynamic::DynamicSpace space;
    CHECK_EQ(space.edgeBlocks(), 65536);

    spatial::dynamic::Position3f p{32767.0f, -32768.0f, 0.0f};
    CHECK(!space.needsRebase(p));
    p.x = 32768.0f;
    const auto d = space.rebaseDeltaFor(p);
    CHECK_EQ(d.x, 65536);
    space.applyRebase(p, d);
    CHECK_EQ(p.x, -32768.0f);
}

TEST_CASE(dynamic_space_and_world_bridge_preserve_position_across_rebase)
{
    using namespace world;
    spatial::dynamic::DynamicSpace space;
    spatial::bridge::WorldDynamicBridge bridge(originBlockAddress());
    spatial::dynamic::Position3f p{32768.25f, 12.5f, -3.75f};

    const WorldPosition before = bridge.toWorld(p);
    const auto delta = space.rebaseDeltaFor(p);
    CHECK(delta.any());
    bridge.shiftAnchor(delta);
    space.applyRebase(p, delta);
    const WorldPosition after = bridge.toWorld(p);

    CHECK(before.blockAddress() == after.blockAddress());
    CHECK(std::abs(before.fractionX() - after.fractionX()) < 1e-6f);
    CHECK(std::abs(before.fractionY() - after.fractionY()) < 1e-6f);
    CHECK(std::abs(before.fractionZ() - after.fractionZ()) < 1e-6f);
}

TEST_CASE(dynamic_space_is_independent_of_world_radices)
{
    spatial::dynamic::DynamicSpace custom(32768);
    CHECK_EQ(custom.edgeBlocks(), 32768);
    spatial::dynamic::Position3f p{16384.0f, 0.0f, 0.0f};
    const auto d = custom.rebaseDeltaFor(p);
    CHECK_EQ(d.x, 32768);
}

TEST_CASE(dynamic_space_worst_case_float_spacing_is_one_256th_block)
{
    const float atEdge = 32768.0f;
    const float next = std::nextafter( atEdge, std::numeric_limits<float>::infinity() );
    CHECK( std::abs( ( next - atEdge ) - ( 1.0f / 256.0f ) ) < 1e-9f );
}

int main(){ return test::runAll(); }
