#include "TestHarness.h"
#include "world/interaction/BlockPicker.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

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

TEST_CASE(block_picker_face_and_adjacent_position)
{
    world::ChunkManager chunks;
    const auto target = world::fromOriginOffset( 4, 4, -4 );
    chunks.setBlock( target, 7u );

    // Ray moving +Z: the ray enters the target cell through its -Z face
    // (the camera looks along +Z; the face of the entered side is -Z).
    const auto camera =
        world::WorldPosition::fromBlockAddress( world::fromOriginOffset( 4, 4, -8 ), 0.5f, 0.5f, 0.5f );
    const auto hit = world::interaction::pickBlock( chunks, camera, 0.0, 0.0, 1.0, 16.0 );
    CHECK( hit.has_value() );
    if( !hit )
        return;
    CHECK( hit->block == target );

    CHECK( hit->face == world::interaction::BlockFace::NegativeZ );
    CHECK( hit->adjacent.has_value() );
    if( hit->adjacent )
        CHECK( *hit->adjacent == world::offsetBlock( target, 0, 0, -1 ) );
}

TEST_CASE(block_picker_tie_traverses_axes_sequentially)
{
    // Round 6 (MAJOR 1): an exact XYZ corner ties steps through the axes in
    // the deterministic order X -> Y -> Z, checking each intermediate cell
    // with its own face - never a diagonal jump.
    const auto camera = world::WorldPosition::fromBlockAddress(
        world::fromOriginOffset( 0, 0, 0 ), 0.5f, 0.5f, 0.5f );
    const auto cellX = world::fromOriginOffset( 1, 0, 0 );
    const auto cellY = world::fromOriginOffset( 1, 1, 0 );
    const auto cellZ = world::fromOriginOffset( 1, 1, 1 );

    // 1. block only at (1,0,0): the X step must hit it.
    {
        world::ChunkManager chunks;
        chunks.setBlock( cellX, 11u );
        const auto hit = world::interaction::pickBlock( chunks, camera, 1.0, 1.0, 1.0, 8.0 );
        CHECK( hit.has_value() );
        if( !hit )
            return;
        CHECK( hit->block == cellX );
        CHECK( hit->face == world::interaction::BlockFace::NegativeX );
        if( hit->adjacent )
            CHECK( *hit->adjacent == world::fromOriginOffset( 0, 0, 0 ) );
    }
    // 2. (1,0,0) AIR: the Y step must hit (1,1,0).
    {
        world::ChunkManager chunks;
        chunks.setBlock( cellY, 12u );
        const auto hit = world::interaction::pickBlock( chunks, camera, 1.0, 1.0, 1.0, 8.0 );
        CHECK( hit.has_value() );
        if( !hit )
            return;
        CHECK( hit->block == cellY );
        CHECK( hit->face == world::interaction::BlockFace::NegativeY );
        if( hit->adjacent )
            CHECK( *hit->adjacent == cellX );
    }
    // 3. first two candidates AIR: the Z step hits (1,1,1).
    {
        world::ChunkManager chunks;
        chunks.setBlock( cellZ, 13u );
        const auto hit = world::interaction::pickBlock( chunks, camera, 1.0, 1.0, 1.0, 8.0 );
        CHECK( hit.has_value() );
        if( !hit )
            return;
        CHECK( hit->block == cellZ );
        CHECK( hit->face == world::interaction::BlockFace::NegativeZ );
        if( hit->adjacent )
            CHECK( *hit->adjacent == cellY );
    }
    // 4. repeated ray: bit-identical result (deterministic tie-case).
    {
        world::ChunkManager chunks;
        chunks.setBlock( cellY, 12u );
        const auto first = world::interaction::pickBlock( chunks, camera, 1.0, 1.0, 1.0, 8.0 );
        const auto second = world::interaction::pickBlock( chunks, camera, 1.0, 1.0, 1.0, 8.0 );
        CHECK( first.has_value() );
        if( first && second )
        {
            CHECK( first->block == second->block );
            CHECK( first->face == second->face );
            CHECK( first->adjacent == second->adjacent );
        }
    }
}

TEST_CASE(block_picker_face_across_chunk_boundary)
{
    world::ChunkManager chunks;
    // Block on the far side of a chunk boundary; the ray crosses the chunk
    // carry and reports the canonical adjacent cell beyond the face.
    const auto target = world::fromOriginOffset(
        world::BLOCKS_PER_CHUNK_EDGE, 0, 0 ); // first block of the next chunk
    chunks.setBlock( target, 13u );
    const auto camera = world::WorldPosition::fromBlockAddress(
        world::fromOriginOffset( world::BLOCKS_PER_CHUNK_EDGE - 2, 0, 0 ),
        0.5f, 0.5f, 0.5f );
    const auto hit = world::interaction::pickBlock( chunks, camera, 1.0, 0.0, 0.0, 8.0 );
    CHECK( hit.has_value() );
    if( !hit )
        return;
    CHECK( hit->block == target );
    CHECK( hit->face == world::interaction::BlockFace::NegativeX );
    if( hit->adjacent )
        CHECK( *hit->adjacent == world::offsetBlock( target, -1, 0, 0 ) );
}

TEST_CASE(block_picker_overflows_terminate_cleanly)
{
    // Round 7 (MAJOR): a canonical step beyond the representable sector
    // edge must end the traversal with nullopt - no infinite loop, no wrap,
    // no clamp, no invented neighbour. No block occupancy is required.

    // Maximal positive X edge.
    {
        world::BlockAddress far;
        far.chunk.group.sector.x = std::numeric_limits<std::int64_t>::max();
        far.chunk.group.region.x = world::REGIONS_PER_SECTOR_EDGE - 1;
        far.chunk.group.section.x = world::SECTIONS_PER_REGION_EDGE - 1;
        far.chunk.group.group.x = world::GROUPS_PER_SECTION_EDGE - 1;
        far.chunk.chunk.x = world::CHUNKS_PER_GROUP_EDGE - 1;
        far.block.x = world::BLOCKS_PER_CHUNK_EDGE - 1;
        const auto camera = world::WorldPosition::fromBlockAddress( far, 0.5f, 0.5f, 0.5f );
        const auto hit = world::interaction::pickBlock( world::ChunkManager{},
                                                        camera, 1.0, 0.0, 0.0, 2.0 );
        CHECK( !hit.has_value() );
    }

    // Minimal negative X edge.
    {
        world::BlockAddress far;
        far.chunk.group.sector.x = std::numeric_limits<std::int64_t>::min();
        far.chunk.group.region.x = 0;
        far.chunk.group.section.x = 0;
        far.chunk.group.group.x = 0;
        far.chunk.chunk.x = 0;
        far.block.x = 0;
        const auto camera = world::WorldPosition::fromBlockAddress( far, 0.5f, 0.5f, 0.5f );
        const auto hit =
            world::interaction::pickBlock( world::ChunkManager{}, camera, -1.0, 0.0, 0.0, 2.0 );
        CHECK( !hit.has_value() );
    }
}

int main() { return test::runAll(); }
