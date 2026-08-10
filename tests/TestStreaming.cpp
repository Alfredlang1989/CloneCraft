#include "TestHarness.h"
#include "TestWorldGenFixture.h"
#include "world/chunk/ChunkStreamingManager.h"
#include "world/coordinates/Coords.h"

TEST_CASE(streaming_loads_cube_around_hierarchical_center)
{
    auto blocks=testfixture::blocks(); world::BlockIdTable table(blocks);
    auto cfg=testfixture::config(); worldgen::WorldGen gen(cfg,blocks,table); world::ChunkManager chunks;
    world::ChunkStreamingManager stream(gen,chunks,1,64);
    const auto p=world::fromOriginOffset(15,5,15); stream.update(p); stream.flush();
    CHECK_EQ(chunks.chunkCount(),27u);
}

TEST_CASE(streaming_crosses_sector_boundary)
{
    auto blocks=testfixture::blocks(); world::BlockIdTable table(blocks);
    auto cfg=testfixture::config(); worldgen::WorldGen gen(cfg,blocks,table); world::ChunkManager chunks;
    world::ChunkStreamingManager stream(gen,chunks,1,64);
    world::BlockAddress p{}; p.chunk.group.sector.x=7999999999999999999LL;
    p.chunk.group.region.x=world::REGIONS_PER_SECTOR_EDGE-1;
    p.chunk.group.group.x=world::GROUPS_PER_REGION_EDGE-1;
    p.chunk.chunk.x=world::CHUNKS_PER_GROUP_EDGE-1; p.block.x=world::BLOCKS_PER_CHUNK_EDGE-1;
    stream.update(p); stream.flush();
    const auto q=world::offsetBlock(p,1,0,0); stream.update(q); stream.flush();
    CHECK_EQ(chunks.chunkCount(),27u);
}
int main(){return test::runAll();}
