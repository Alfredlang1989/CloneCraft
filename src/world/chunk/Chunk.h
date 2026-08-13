#pragma once

#include "world/coordinates/Coords.h"
#include "world/chunk/OrientationSidecar.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
            const std::uint32_t localIndex = blockIndex( { lx, ly, lz } );
            std::uint16_t &slot = mBlocks[localIndex];
            if( slot == id ) return;
            if( slot == 0 && id != 0 ) ++mNonAirCount;
            else if( slot != 0 && id == 0 ) --mNonAirCount;
            slot = id;
            // Sidecar state describes the *current* block at this position.
            // Replacing the block (including by AIR) invalidates it, so the
            // entry is dropped here. Without this a removed block would leave
            // a zombie orientation behind (issue #3 section 5 invariant).
            if( mOrientation && mOrientation->set( localIndex, BlockOrientation::Up ) &&
                mOrientation->empty() )
                mOrientation.reset();
        }

        void assignBlocks( std::span<const std::uint16_t> blocks )
        {
            if( blocks.size() != VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: wrong block count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = static_cast<std::uint32_t>(
                std::count_if( mBlocks.begin(), mBlocks.end(), []( std::uint16_t id ) { return id != 0; } ) );
            clearOrientations(); // wholesale content replacement invalidates sidecars
        }

        void assignBlocks( std::span<const std::uint16_t> blocks, std::uint32_t nonAirCount )
        {
            if( blocks.size() != VOLUME || nonAirCount > VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: invalid block data/count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = nonAirCount;
            clearOrientations(); // wholesale content replacement invalidates sidecars
        }

        bool empty() const { return mNonAirCount == 0; }
        std::uint32_t nonAirCount() const { return mNonAirCount; }
        const std::array<std::uint16_t, VOLUME> &data() const { return mBlocks; }

        // -- Sparse chunk sidecars (issue #3, section 5) ----------------------

        /** Sets the block orientation. Writing the default (Up) removes the
         *  entry; when the last entry disappears the sidecar is dropped.
         *  @return true when the stored state actually changed.
         *  @note Invariant (issue #3 section 5): a sidecar exists only while
         *        at least one block actually needs its state. AIR blocks can
         *        never carry orientation, so writes to AIR positions are
         *        rejected here (returns false, nothing stored). */
        bool setBlockOrientation( std::int64_t lx, std::int64_t ly, std::int64_t lz,
                                  BlockOrientation orientation )
        {
            const std::uint32_t localIndex = blockIndex( { lx, ly, lz } );
            if( mBlocks[localIndex] == 0u )
                return false; // AIR: a sidecar entry would be zombie state
            if( !mOrientation )
            {
                if( orientation == BlockOrientation::Up )
                    return false; // default on default: no state change
                mOrientation = std::make_unique<OrientationSidecar>( VOLUME );
            }
            const bool changed = mOrientation->set( localIndex, orientation );
            if( mOrientation->empty() )
                mOrientation.reset();
            return changed;
        }

        /** Absent entry means the block has the default orientation (Up). */
        std::optional<BlockOrientation> blockOrientation( std::int64_t lx, std::int64_t ly,
                                                          std::int64_t lz ) const
        {
            if( !mOrientation )
                return std::nullopt;
            return mOrientation->get( blockIndex( { lx, ly, lz } ) );
        }

        /** nullptr while the chunk has no oriented blocks. */
        const OrientationSidecar *orientationSidecar() const { return mOrientation.get(); }

        void clearOrientations() { mOrientation.reset(); }

    private:
        ChunkAddress mAddress{};
        std::array<std::uint16_t, VOLUME> mBlocks{};
        std::uint32_t mNonAirCount = 0;
        std::unique_ptr<OrientationSidecar> mOrientation; // lazy: exists only when needed
    };
} // namespace world
