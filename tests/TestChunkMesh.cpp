#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMeshBuilder.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"

namespace {
world::BlockRegistry blocks()
{
    world::BlockRegistry r;
    world::BlockDef air; air.id="core:air"; air.displayName="Air"; air.opaque=false; air.transparent=true; r.insert(air);
    world::BlockDef stone; stone.id="core:stone"; stone.displayName="Stone"; stone.opaque=true; stone.solid=true; r.insert(stone);
    world::BlockDef flower; flower.id="core:flower"; flower.displayName="Flower"; flower.opaque=false; flower.renderShape=world::BlockRenderShape::Cross; r.insert(flower);
    return r;
}
}

TEST_CASE(single_cube_has_six_quads)
{
    auto b=blocks(); world::BlockIdTable table(b); world::ChunkManager m;
    const auto p=world::fromOriginOffset(0,0,0); m.setBlock(p,table.indexOf("core:stone"));
    world::ChunkMeshBuilder builder(table,b); world::ChunkMesh mesh; builder.build(m,p.chunk,mesh);
    CHECK_EQ(mesh.quadCount(),6u);
}

TEST_CASE(cross_block_has_four_quads)
{
    auto b=blocks(); world::BlockIdTable table(b); world::ChunkManager m;
    const auto p=world::fromOriginOffset(0,0,0); m.setBlock(p,table.indexOf("core:flower"));
    world::ChunkMeshBuilder builder(table,b); world::ChunkMesh mesh; builder.build(m,p.chunk,mesh);
    CHECK_EQ(mesh.quadCount(),4u);
}
int main(){return test::runAll();}
