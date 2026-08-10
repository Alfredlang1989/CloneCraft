#pragma once

#include "world/coordinates/Coords.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace world
{
    class Chunk
    {
    public:
        static constexpr std::int64_t EDGE = BLOCKS_PER_CHUNK_EDGE;
        static constexpr std::size_t VOLUME =
            static_cast<std::size_t>( EDGE ) * static_cast<std::size_t>( EDGE ) *
            static_cast<std::size_t>( EDGE );

        explicit Chunk( const ChunkAddress &address ) : mAddress( address ) { requireCanonical( address ); }

        Chunk( const Chunk & ) = delete;
        Chunk &operator=( const Chunk & ) = delete;

        ChunkAddress address() const { return mAddress; }
        ChunkAddress coord() const { return mAddress; } // compatibility name: still hierarchical

        std::uint16_t block( std::int64_t lx, std::int64_t ly, std::int64_t lz ) const
        {
            return mBlocks[blockIndex( { lx, ly, lz } )];
        }

        void setBlock( std::int64_t lx, std::int64_t ly, std::int64_t lz, std::uint16_t id )
        {
            std::uint16_t &slot = mBlocks[blockIndex( { lx, ly, lz } )];
            if( slot == id ) return;
            if( slot == 0 && id != 0 ) ++mNonAirCount;
            else if( slot != 0 && id == 0 ) --mNonAirCount;
            slot = id;
        }

        void assignBlocks( std::span<const std::uint16_t> blocks )
        {
            if( blocks.size() != VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: wrong block count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = static_cast<std::uint32_t>(
                std::count_if( mBlocks.begin(), mBlocks.end(), []( std::uint16_t id ) { return id != 0; } ) );
        }

        void assignBlocks( std::span<const std::uint16_t> blocks, std::uint32_t nonAirCount )
        {
            if( blocks.size() != VOLUME || nonAirCount > VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: invalid block data/count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = nonAirCount;
        }

        bool empty() const { return mNonAirCount == 0; }
        std::uint32_t nonAirCount() const { return mNonAirCount; }
        const std::array<std::uint16_t, VOLUME> &data() const { return mBlocks; }

    private:
        ChunkAddress mAddress{};
        std::array<std::uint16_t, VOLUME> mBlocks{};
        std::uint32_t mNonAirCount = 0;
    };
} // namespace world
