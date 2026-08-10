#include "world/chunk/ChunkGroup.h"

#include <cassert>

namespace world
{
    ChunkGroup::ChunkGroup( const GroupAddress &address ) : mAddress( address ) { requireCanonical( address ); }

    Chunk *ChunkGroup::findChunk( std::uint32_t localIndex )
    {
        const auto it = mChunks.find( localIndex );
        return it == mChunks.end() ? nullptr : it->second.get();
    }
    const Chunk *ChunkGroup::findChunk( std::uint32_t localIndex ) const
    {
        const auto it = mChunks.find( localIndex );
        return it == mChunks.end() ? nullptr : it->second.get();
    }

    Chunk *ChunkGroup::getOrCreateChunk( std::uint32_t localIndex )
    {
        assert( localIndex < static_cast<std::uint32_t>( groupVolume() ) );
        if( Chunk *existing = findChunk( localIndex ) ) return existing;

        const std::int64_t e = CHUNKS_PER_GROUP_EDGE;
        const LocalChunkCoord local{
            static_cast<std::int64_t>( localIndex ) / ( e * e ),
            ( static_cast<std::int64_t>( localIndex ) / e ) % e,
            static_cast<std::int64_t>( localIndex ) % e };
        auto chunk = std::make_unique<Chunk>( ChunkAddress{ mAddress, local } );
        Chunk *ptr = chunk.get();
        mChunks[localIndex] = std::move( chunk );
        return ptr;
    }

    bool ChunkGroup::removeChunk( std::uint32_t localIndex )
    {
        return mChunks.erase( localIndex ) > 0;
    }
} // namespace world
