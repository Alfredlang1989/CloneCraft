#pragma once

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkGroup.h"
#include "world/chunk/OrientationSidecar.h"

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
        /** Central block mutation. @return true when the block actually changed. */
        bool setBlock( const BlockAddress &, std::uint16_t blockId );

        // Generic data-driven sidecar state (issue #3, section 5; M05
        // resolver). Sidecar state never creates or unloads chunks; absent
        // chunks simply have no sidecar. The caller supplies the type's
        // declared default so ChunkManager stays content-agnostic.
        bool setBlockProperty( const BlockAddress &, const std::string &typeId,
                               const PropertyValue &value, const PropertyValue &defaultValue );
        std::optional<PropertyValue> blockProperty( const BlockAddress &,
                                                    const std::string &typeId ) const;
        const Sidecar<PropertyValue> *chunkPropertySidecar( const ChunkAddress &,
                                                            const std::string &typeId ) const;
        /** @return true when a sidecar of the type actually existed. */
        bool clearChunkProperty( const ChunkAddress &, const std::string &typeId );

        // Orientation pilot shim (compat + convenience, issue #3 section 5).
        // Stored through the generic sidecar path under the data-driven id
        // "core:orientation" (CORE_ORIENTATION_SIDECAR), so the unified world
        // state resolver sees exactly the same state. The setters return
        // false when nothing changed (no notification is emitted for no-ops).
        bool setBlockOrientation( const BlockAddress &, BlockOrientation );
        std::optional<BlockOrientation> blockOrientation( const BlockAddress & ) const;
        const Sidecar<PropertyValue> *chunkOrientationSidecar( const ChunkAddress & ) const;
        void clearChunkOrientations( const ChunkAddress & );

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
        /** Notifies the block's chunk and, for boundary blocks, the directly
         *  adjacent chunks (mesh/neighbour invalidation, M05). */
        void notifyChangeForBlock( const BlockAddress & );

        std::map<GroupAddress, ChunkGroup> mGroups;
        ChangeCallback mOnChunkChange;
    };
} // namespace world
