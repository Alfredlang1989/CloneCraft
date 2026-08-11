#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/worldgen/WorldGenConfig.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace worldgen
{
    struct BlockDelta
    {
        world::BlockAddress pos;
        std::string blockId;
    };

    struct BlockProposal
    {
        std::size_t localIndex = 0;
        std::uint16_t blockId = 0;
        std::int32_t priority = 0;
        std::uint32_t passOrder = 0;
    };

    class WorldGen
    {
    public:
        WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                  const world::BlockIdTable &table );
        WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                  const world::BlockIdTable &table, const world::BiomeRegistry &biomes );
        ~WorldGen();
        WorldGen( const WorldGen & ) = delete;
        WorldGen &operator=( const WorldGen & ) = delete;

        void generateChunk( const world::ChunkAddress &chunk, std::vector<BlockDelta> &out ) const;
        std::uint32_t generateChunkIds( const world::ChunkAddress &chunk,
                                        std::span<std::uint16_t> out ) const;

        /** Height is an origin-relative scalar field value, not a flattened world Y. */
        std::int64_t surfaceHeight( const world::BlockAddress &column ) const;

        std::uint32_t workerThreads() const;
        std::size_t fieldCount() const;
        std::size_t stageCount() const;
        std::size_t passCount() const;
        std::size_t anchorSetCount() const;
        std::size_t decorationPassCount() const;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
} // namespace worldgen
