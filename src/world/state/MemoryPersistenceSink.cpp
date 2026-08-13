#include "world/state/MemoryPersistenceSink.h"

#include <algorithm>

namespace world
{
    void MemoryPersistenceSink::onBlockChanged( const BlockAddress &address,
                                                std::uint16_t oldRuntimeId,
                                                std::uint16_t newRuntimeId )
    {
        mBlockDeltas[address] = BlockDelta{ address, oldRuntimeId, newRuntimeId };
        mDirtyChunks.insert( address.chunk );
    }

    void MemoryPersistenceSink::onPropertyChanged( const BlockAddress &address,
                                                   const std::string &propertyId )
    {
        mPropertyDeltas[{ address, propertyId }] = PropertyDelta{ address, propertyId };
        mDirtyChunks.insert( address.chunk );
    }

    void MemoryPersistenceSink::flush()
    {
        mBlockDeltas.clear();
        mPropertyDeltas.clear();
        mDirtyChunks.clear();
    }

    bool MemoryPersistenceSink::isDirty( const ChunkAddress &address ) const
    {
        return mDirtyChunks.count( address ) != 0u;
    }

    std::vector<ChunkAddress> MemoryPersistenceSink::dirtyChunks() const
    {
        std::vector<ChunkAddress> result( mDirtyChunks.begin(), mDirtyChunks.end() );
        std::sort( result.begin(), result.end() );
        return result;
    }
} // namespace world
