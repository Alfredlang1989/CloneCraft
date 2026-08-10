#pragma once

#include "world/chunk/ChunkGroup.h"

#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace world
{
    /** Sparse hierarchical chunk storage. No flattened world coordinate is used. */
    class ChunkManager
    {
    public:
        using ChangeCallback = std::function<void( const ChunkAddress & )>;
        void setOnChunkChange( ChangeCallback callback ) { mOnChunkChange = std::move( callback ); }

        Chunk *chunkAt( const ChunkAddress & );
        const Chunk *chunkAt( const ChunkAddress & ) const;
        Chunk *loadChunk( const ChunkAddress & );
        bool unloadChunk( const ChunkAddress & );

        std::optional<std::uint16_t> tryBlockAt( const BlockAddress & ) const;
        std::uint16_t blockAt( const BlockAddress & ) const;
        void setBlock( const BlockAddress &, std::uint16_t blockId );

        std::size_t groupCount() const { return mGroups.size(); }
        std::size_t chunkCount() const;

        template <typename F>
        void forEachChunk( F &&cb )
        {
            for( auto &[address, group] : mGroups )
            {
                (void)address;
                group.forEachChunk( cb );
            }
        }

    private:
        Chunk *getOrCreateChunk( const ChunkAddress & );
        void notifyChange( const ChunkAddress & );
        void notifyChangeWithNeighbors( const ChunkAddress & );

        std::map<GroupAddress, ChunkGroup> mGroups;
        ChangeCallback mOnChunkChange;
    };
} // namespace world
