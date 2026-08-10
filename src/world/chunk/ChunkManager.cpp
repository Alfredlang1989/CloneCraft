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

    void ChunkManager::setBlock( const BlockAddress &b, std::uint16_t blockId )
    {
        Chunk *chunk = getOrCreateChunk( b.chunk );
        const std::uint16_t previous = chunk->block( b.block.x, b.block.y, b.block.z );
        if( previous == blockId ) return;
        chunk->setBlock( b.block.x, b.block.y, b.block.z, blockId );
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
