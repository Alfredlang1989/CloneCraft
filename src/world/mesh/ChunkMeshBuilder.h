#pragma once

#include <cstdint>
#include <vector>

#include "world/coordinates/Coords.h"
#include "world/mesh/ChunkMesh.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"

namespace world
{
    class ChunkManager;

    /**
     * Builds the CPU-side mesh of one chunk. Visible coplanar faces of the
     * same block are greedily merged. UVs are expressed in *block texture
     * space* (one UV unit per voxel), not atlas space; therefore a 16x8
     * greedy quad gets UVs spanning 16x8 and the renderer can safely repeat
     * the selected block texture with TAM_WRAP.
     *
     * This is deliberately renderer-independent. Each emitted vertex keeps
     * its runtime block id so the renderer can split one ChunkMesh into
     * per-block material sections without losing greedy merging.
     */
    class ChunkMeshBuilder
    {
    public:
        ChunkMeshBuilder( const BlockIdTable &table, const BlockRegistry &blocks );

        void build( const ChunkManager &world, const ChunkAddress &chunk,
                    ChunkMesh &mesh ) const;

    private:
        const BlockIdTable &mTable;
        // Cached once per builder instead of reconstructing registry lookups
        // for every chunk mesh. Index is runtime BlockId.
        std::vector<std::uint8_t> mOpaque;
        std::vector<std::uint8_t> mNeedsTangent;
        std::vector<std::uint8_t> mCrossShape;
    };
} // namespace world
