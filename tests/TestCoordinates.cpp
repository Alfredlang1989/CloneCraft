#include "TestHarness.h"
#include "world/coordinates/Coords.h"
#include "world/coordinates/StickyGroupAnchor.h"
#include "world/coordinates/WorldPosition.h"
#include <cstdint>
#include <limits>

TEST_CASE(canonical_carry_crosses_all_levels)
{
    using namespace world;
    BlockAddress a{};
    a.chunk.group.sector={10,10,10}; a.chunk.group.region={REGIONS_PER_SECTOR_EDGE-1,REGIONS_PER_SECTOR_EDGE-1,REGIONS_PER_SECTOR_EDGE-1};
    a.chunk.group.group={GROUPS_PER_REGION_EDGE-1,GROUPS_PER_REGION_EDGE-1,GROUPS_PER_REGION_EDGE-1};
    a.chunk.chunk={CHUNKS_PER_GROUP_EDGE-1,CHUNKS_PER_GROUP_EDGE-1,CHUNKS_PER_GROUP_EDGE-1};
    a.block={BLOCKS_PER_CHUNK_EDGE-1,BLOCKS_PER_CHUNK_EDGE-1,BLOCKS_PER_CHUNK_EDGE-1};
    const auto b=offsetBlock(a,1,1,1);
    CHECK_EQ(b.chunk.group.sector.x,11); CHECK_EQ(b.chunk.group.region.x,0);
    CHECK_EQ(b.chunk.group.group.x,0); CHECK_EQ(b.chunk.chunk.x,0); CHECK_EQ(b.block.x,0);
    CHECK(offsetBlock(b,-1,-1,-1)==a);
}

TEST_CASE(origin_offsets_handle_negative_values)
{
    using namespace world;
    const auto a=fromOriginOffset(-1,-17,-257);
    const auto o=originBlockAddress(); RelativeI64 d{};
    CHECK(blockDeltaWithin(a,o,300,d));
    CHECK_EQ(d.x,-1); CHECK_EQ(d.y,-17); CHECK_EQ(d.z,-257);
}

TEST_CASE(relative_delta_across_sector_seam_is_small)
{
    using namespace world;
    BlockAddress a{}; a.chunk.group.sector.x=5; a.chunk.group.region.x=REGIONS_PER_SECTOR_EDGE-1;
    a.chunk.group.group.x=GROUPS_PER_REGION_EDGE-1;
    a.chunk.chunk.x=CHUNKS_PER_GROUP_EDGE-1; a.block.x=BLOCKS_PER_CHUNK_EDGE-1;
    const auto b=offsetBlock(a,1,0,0); RelativeI64 d{};
    CHECK(blockDeltaWithin(b,a,1,d)); CHECK_EQ(d.x,1);
}

TEST_CASE(world_position_keeps_fraction_at_huge_sector)
{
    using namespace world;
    BlockAddress a{}; a.chunk.group.sector={8000000000000000000LL,0,-8000000000000000000LL};
    WorldPosition p=WorldPosition::fromBlockAddress(a,0.75f,0.25f,0.5f);
    p.translate(0.5,0.0,0.0);
    CHECK_EQ(p.blockAddress().block.x,1); CHECK(std::abs(p.fractionX()-0.25f)<1e-6f);
    const auto local=p.relativeToGroup(p.group()); CHECK(local.x>=1.0f && local.x<2.0f);
}

TEST_CASE(top_sector_overflow_never_wraps)
{
    using namespace world;
    BlockAddress a{}; a.chunk.group.sector.x=std::numeric_limits<std::int64_t>::max();
    a.chunk.group.region.x=REGIONS_PER_SECTOR_EDGE-1;
    a.chunk.group.group.x=GROUPS_PER_REGION_EDGE-1;
    a.chunk.chunk.x=CHUNKS_PER_GROUP_EDGE-1; a.block.x=BLOCKS_PER_CHUNK_EDGE-1;
    BlockAddress out{}; CHECK(!tryOffsetBlock(a,1,0,0,out));
}

TEST_CASE(group_local_block_mapping_stays_small)
{
    using namespace world;
    BlockAddress a{};
    a.chunk.group.sector={8000000000000000000LL,-8,-8000000000000000000LL};
    a.chunk.chunk={15,7,3};
    a.block={15,4,12};
    const LocalGroupBlockCoord local=localBlockInGroup(a);
    CHECK_EQ(local.x,255);
    CHECK_EQ(local.y,116);
    CHECK_EQ(local.z,60);
}

int main(){return test::runAll();}
