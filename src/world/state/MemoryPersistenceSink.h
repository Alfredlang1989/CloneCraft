#pragma once

#include "world/state/PersistenceSink.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace world
{
    /**
     * In-memory persistence sink (M05): records dirty chunks and
     * last-write-wins block/property deltas without any database. This is the
     * reference implementation of PersistenceSink; M09 replaces it with the
     * RocksDB backend while the WorldState contract stays unchanged.
     */
    class MemoryPersistenceSink : public PersistenceSink
    {
    public:
        void onBlockChanged( const BlockAddress &, std::uint16_t oldRuntimeId,
                             std::uint16_t newRuntimeId ) override;
        void onPropertyChanged( const BlockAddress &, const std::string &propertyId,
                                std::optional<PropertyValue> value ) override;
        void flush() override;

        // -- introspection (tests / M09 migration probe) ----------------------
        std::size_t blockDeltaCount() const { return mBlockDeltas.size(); }
        std::size_t propertyDeltaCount() const { return mPropertyDeltas.size(); }
        std::size_t dirtyChunkCount() const { return mDirtyChunks.size(); }
        bool isDirty( const ChunkAddress & ) const;
        std::vector<ChunkAddress> dirtyChunks() const;
        const std::map<BlockAddress, BlockDelta> &blockDeltas() const { return mBlockDeltas; }
        const std::map<std::pair<BlockAddress, std::string>, PropertyDelta> &propertyDeltas() const
        {
            return mPropertyDeltas;
        }

    private:
        std::map<BlockAddress, BlockDelta> mBlockDeltas;
        std::map<std::pair<BlockAddress, std::string>, PropertyDelta> mPropertyDeltas;
        std::set<ChunkAddress> mDirtyChunks;
    };
} // namespace world
