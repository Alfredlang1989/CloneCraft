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
    world::BlockDef tintable; tintable.id="test:tintable"; tintable.displayName="Tintable"; tintable.opaque=true; tintable.solid=true; tintable.visualTintProperty="test:tint"; r.insert(tintable);
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

TEST_CASE(cube_quads_record_the_emitting_voxel_for_all_six_faces)
{
    auto b=blocks(); world::BlockIdTable table(b); world::ChunkManager m;
    const auto p=world::fromOriginOffset(2,3,4);
    m.setBlock(p,table.indexOf("core:stone"));
    world::ChunkMeshBuilder builder(table,b); world::ChunkMesh mesh;
    builder.build(m,p.chunk,mesh);
    CHECK_EQ(mesh.quadCount(),6u);

    bool directions[6] = { false, false, false, false, false, false };
    for(std::size_t base=0;base+3u<mesh.vertices.size();base+=4u)
    {
        const auto &v=mesh.vertices[base];
        CHECK_EQ(v.ownerX,2);
        CHECK_EQ(v.ownerY,3);
        CHECK_EQ(v.ownerZ,4);
        for(std::size_t i=1;i<4u;++i)
        {
            CHECK_EQ(mesh.vertices[base+i].ownerX,v.ownerX);
            CHECK_EQ(mesh.vertices[base+i].ownerY,v.ownerY);
            CHECK_EQ(mesh.vertices[base+i].ownerZ,v.ownerZ);
        }
        if(v.nx>0.5f) directions[0]=true;
        else if(v.nx< -0.5f) directions[1]=true;
        else if(v.ny>0.5f) directions[2]=true;
        else if(v.ny< -0.5f) directions[3]=true;
        else if(v.nz>0.5f) directions[4]=true;
        else if(v.nz< -0.5f) directions[5]=true;
    }
    for(bool seen:directions) CHECK(seen);
}

TEST_CASE(tintable_neighbors_keep_distinct_quad_owners)
{
    auto b=blocks(); world::BlockIdTable table(b); world::ChunkManager m;
    const auto left=world::fromOriginOffset(2,3,4);
    const auto right=world::fromOriginOffset(3,3,4);
    const auto id=table.indexOf("test:tintable");
    m.setBlock(left,id); m.setBlock(right,id);
    world::ChunkMeshBuilder builder(table,b); world::ChunkMesh mesh;
    builder.build(m,left.chunk,mesh);

    // The shared face is culled, but visible coplanar faces are not greedily
    // merged across tint-capable voxels: each face retains its own owner.
    CHECK_EQ(mesh.quadCount(),10u);
    bool sawLeftTop=false;
    bool sawRightTop=false;
    for(std::size_t base=0;base+3u<mesh.vertices.size();base+=4u)
    {
        const auto &v=mesh.vertices[base];
        if(v.ny>0.5f && v.ownerX==2 && v.ownerY==3 && v.ownerZ==4) sawLeftTop=true;
        if(v.ny>0.5f && v.ownerX==3 && v.ownerY==3 && v.ownerZ==4) sawRightTop=true;
    }
    CHECK(sawLeftTop);
    CHECK(sawRightTop);
}
int main(){return test::runAll();}
