#include "world/chunk/ChunkManager.h"

namespace world
{
    void ChunkManager::notifyChange( const ChunkAddress &c )
    {
        if( mOnChunkChange ) mOnChunkChange( c );
    }

    void ChunkManager::notifyChangeWithNeighbors( const ChunkAddress &c )
    {
        notifyChange( c );
        ChunkAddress n;
        if( tryOffsetChunk( c, -1, 0, 0, n ) ) notifyChange( n );
        if( tryOffsetChunk( c,  1, 0, 0, n ) ) notifyChange( n );
        if( tryOffsetChunk( c, 0, -1, 0, n ) ) notifyChange( n );
        if( tryOffsetChunk( c, 0,  1, 0, n ) ) notifyChange( n );
        if( tryOffsetChunk( c, 0, 0, -1, n ) ) notifyChange( n );
        if( tryOffsetChunk( c, 0, 0,  1, n ) ) notifyChange( n );
    }

    void ChunkManager::notifyChangeForBlock( const BlockAddress &b )
    {
        notifyChange( b.chunk );
        const std::int64_t edge = BLOCKS_PER_CHUNK_EDGE - 1;
        ChunkAddress n;
        if( b.block.x == 0 && tryOffsetChunk( b.chunk, -1, 0, 0, n ) ) notifyChange( n );
        if( b.block.x == edge && tryOffsetChunk( b.chunk, 1, 0, 0, n ) ) notifyChange( n );
        if( b.block.y == 0 && tryOffsetChunk( b.chunk, 0, -1, 0, n ) ) notifyChange( n );
        if( b.block.y == edge && tryOffsetChunk( b.chunk, 0, 1, 0, n ) ) notifyChange( n );
        if( b.block.z == 0 && tryOffsetChunk( b.chunk, 0, 0, -1, n ) ) notifyChange( n );
        if( b.block.z == edge && tryOffsetChunk( b.chunk, 0, 0, 1, n ) ) notifyChange( n );
    }

    Chunk *ChunkManager::chunkAt( const ChunkAddress &c )
    {
        const auto it = mGroups.find( c.group );
        if( it == mGroups.end() ) return nullptr;
        return it->second.findChunk( chunkIndex( c.chunk ) );
    }

    const Chunk *ChunkManager::chunkAt( const ChunkAddress &c ) const
    {
        const auto it = mGroups.find( c.group );
        if( it == mGroups.end() ) return nullptr;
        return it->second.findChunk( chunkIndex( c.chunk ) );
    }

    Chunk *ChunkManager::getOrCreateChunk( const ChunkAddress &c )
    {
        ChunkGroup &group = mGroups.try_emplace( c.group, c.group ).first->second;
        return group.getOrCreateChunk( chunkIndex( c.chunk ) );
    }

    Chunk *ChunkManager::loadChunk( const ChunkAddress &c )
    {
        if( Chunk *existing = chunkAt( c ) ) return existing;
        Chunk *chunk = getOrCreateChunk( c );
        notifyChangeWithNeighbors( c );
        return chunk;
    }

    bool ChunkManager::unloadChunk( const ChunkAddress &c )
    {
        const auto it = mGroups.find( c.group );
        if( it == mGroups.end() ) return false;
        const bool removed = it->second.removeChunk( chunkIndex( c.chunk ) );
        if( it->second.chunkCount() == 0 ) mGroups.erase( it );
        if( removed ) notifyChangeWithNeighbors( c );
        return removed;
    }

    std::optional<std::uint16_t> ChunkManager::tryBlockAt( const BlockAddress &b ) const
    {
        const Chunk *chunk = chunkAt( b.chunk );
        if( !chunk ) return std::nullopt;
        return chunk->block( b.block.x, b.block.y, b.block.z );
    }

    std::uint16_t ChunkManager::blockAt( const BlockAddress &b ) const
    {
        return tryBlockAt( b ).value_or( 0u );
    }

    bool ChunkManager::setBlock( const BlockAddress &b, std::uint16_t blockId )
    {
        Chunk *chunk = chunkAt( b.chunk );
        if( !chunk )
        {
            // Writing AIR into an unloaded position is a vacuous no-op: never
            // materialize a chunk just to learn that an all-air write changed
            // nothing (empty-chunk graveyards from no-op actions, M05 round 2).
            if( blockId == 0u )
                return false;
            chunk = getOrCreateChunk( b.chunk );
        }
        const std::uint16_t previous = chunk->block( b.block.x, b.block.y, b.block.z );
        if( previous == blockId ) return false;
        chunk->setBlock( b.block.x, b.block.y, b.block.z, blockId );
        notifyChangeForBlock( b );
        return true;
    }

    bool ChunkManager::setBlockProperty( const BlockAddress &b, const std::string &typeId,
                                         const PropertyValue &value,
                                         const PropertyValue &defaultValue )
    {
        Chunk *chunk = chunkAt( b.chunk );
        if( !chunk )
            return false; // absent chunk: no sidecar to mutate
        const bool changed =
            chunk->setProperty( blockIndex( b.block ), typeId, value, defaultValue );
        if( changed )
            notifyChangeForBlock( b ); // mesh/neighbour invalidation (M05)
        return changed;
    }

    std::optional<PropertyValue> ChunkManager::blockProperty( const BlockAddress &b,
                                                              const std::string &typeId ) const
    {
        const Chunk *chunk = chunkAt( b.chunk );
        if( !chunk )
            return std::nullopt;
        return chunk->getProperty( blockIndex( b.block ), typeId );
    }

    const Sidecar<PropertyValue> *ChunkManager::blockPropertySidecarInChunk(
        const ChunkAddress &a, const std::string &typeId ) const
    {
        const Chunk *chunk = chunkAt( a );
        return chunk ? chunk->propertySidecar( typeId ) : nullptr;
    }

    bool ChunkManager::clearBlockPropertySidecarInChunk( const ChunkAddress &a, const std::string &typeId )
    {
        Chunk *chunk = chunkAt( a );
        if( !chunk )
            return false;
        if( !chunk->clearProperty( typeId ) )
            return false; // nothing to clear: no state change
        notifyChangeWithNeighbors( a );
        return true;
    }

    // -- Orientation pilot shim over the generic sidecar path ---------------

    bool ChunkManager::setBlockOrientation( const BlockAddress &b, BlockOrientation orientation )
    {
        return setBlockProperty( b, CORE_ORIENTATION_SIDECAR,
                                 PropertyValue{ blockOrientationValue( orientation ) },
                                 PropertyValue{ blockOrientationValue( BlockOrientation::Up ) } );
    }

    std::optional<BlockOrientation> ChunkManager::blockOrientation( const BlockAddress &b ) const
    {
        const std::optional<PropertyValue> stored = blockProperty( b, CORE_ORIENTATION_SIDECAR );
        if( !stored || !std::holds_alternative<std::uint32_t>( *stored ) )
            return std::nullopt;
        return blockOrientationFromValue( std::get<std::uint32_t>( *stored ) );
    }

    const Sidecar<PropertyValue> *ChunkManager::chunkOrientationSidecar(
        const ChunkAddress &a ) const
    {
        return blockPropertySidecarInChunk( a, CORE_ORIENTATION_SIDECAR );
    }

    void ChunkManager::clearChunkOrientations( const ChunkAddress &a )
    {
        (void)clearBlockPropertySidecarInChunk( a, CORE_ORIENTATION_SIDECAR );
    }

    std::size_t ChunkManager::chunkCount() const
    {
        std::size_t total = 0;
        for( const auto &[address, group] : mGroups )
        {
            (void)address;
            total += group.chunkCount();
        }
        return total;
    }
} // namespace world
