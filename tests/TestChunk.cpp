#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/coordinates/Coords.h"

TEST_CASE(chunk_manager_stores_hierarchical_blocks)
{
    world::ChunkManager m;
    const auto p=world::fromOriginOffset(17,-1,33);
    m.setBlock(p,7);
    CHECK_EQ(m.blockAt(p),7u);
    CHECK_EQ(m.chunkCount(),1u);
    CHECK(m.chunkAt(p.chunk)!=nullptr);
}

TEST_CASE(neighbor_chunk_across_group_boundary_is_addressed_without_flattening)
{
    world::ChunkManager m;
    world::ChunkAddress c{}; c.chunk={15,0,0};
    const auto n=world::offsetChunk(c,1,0,0);
    CHECK_EQ(n.chunk.x,0); CHECK_EQ(n.group.group.x,1);
    CHECK(m.loadChunk(c)!=nullptr); CHECK(m.loadChunk(n)!=nullptr); CHECK_EQ(m.chunkCount(),2u);
}
int main(){return test::runAll();}
