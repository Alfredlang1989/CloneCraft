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

    void MemoryPersistenceSink::onPropertyChanged( const WorldStateTarget &target,
                                                   const std::string &propertyId,
                                                   std::optional<PropertyValue> value )
    {
        // Block-scope records keep the legacy BlockAddress convenience key;
        // hierarchy scopes (Chunk .. Sector) are keyed by their canonical
        // target identity. Only block-scope changes have a chunk to mark
        // dirty - a hierarchy property never implies a chunk-mesh rebuild.
        if( target.isBlock() )
            mDirtyChunks.insert( target.asBlock().chunk );
        mPropertyDeltas.insert_or_assign( std::pair{ target, propertyId },
                                          PropertyDelta{ target, propertyId, value } );
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
