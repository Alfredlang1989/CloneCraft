#pragma once

#include "world/chunk/Chunk.h"

#include <memory>
#include <unordered_map>

namespace world
{
    class ChunkGroup
    {
    public:
        explicit ChunkGroup( const GroupAddress &address );

        GroupAddress address() const { return mAddress; }
        GroupAddress coord() const { return mAddress; }

        Chunk *findChunk( std::uint32_t localIndex );
        const Chunk *findChunk( std::uint32_t localIndex ) const;
        Chunk *getOrCreateChunk( std::uint32_t localIndex );
        bool removeChunk( std::uint32_t localIndex );
        std::size_t chunkCount() const { return mChunks.size(); }

        template <typename F>
        void forEachChunk( F &&cb )
        {
            for( auto &pair : mChunks ) cb( *pair.second );
        }

    private:
        GroupAddress mAddress{};
        std::unordered_map<std::uint32_t, std::unique_ptr<Chunk>> mChunks;
    };
} // namespace world
