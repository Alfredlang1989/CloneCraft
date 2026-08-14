#pragma once

#include "world/coordinates/Coords.h"
#include "world/chunk/Sidecar.h"
#include "world/registry/Registry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

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
            // a zombie sidecar entry behind (issue #3 section 5 invariant).
            // Removal is explicit (never a default-value write) so no entry
            // can survive at the wrong value when the old logical default
            // differs from the block that previously owned it (M05 round 2).
            for( auto it = mSidecars.begin(); it != mSidecars.end(); )
            {
                it->second->remove( localIndex );
                if( it->second->empty() ) it = mSidecars.erase( it );
                else ++it;
            }
        }

        void assignBlocks( std::span<const std::uint16_t> blocks )
        {
            if( blocks.size() != VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: wrong block count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = static_cast<std::uint32_t>(
                std::count_if( mBlocks.begin(), mBlocks.end(), []( std::uint16_t id ) { return id != 0; } ) );
            clearProperties(); // wholesale content replacement invalidates sidecars
        }

        void assignBlocks( std::span<const std::uint16_t> blocks, std::uint32_t nonAirCount )
        {
            if( blocks.size() != VOLUME || nonAirCount > VOLUME )
                throw std::invalid_argument( "Chunk::assignBlocks: invalid block data/count" );
            std::copy( blocks.begin(), blocks.end(), mBlocks.begin() );
            mNonAirCount = nonAirCount;
            clearProperties(); // wholesale content replacement invalidates sidecars
        }

        bool empty() const { return mNonAirCount == 0; }
        std::uint32_t nonAirCount() const { return mNonAirCount; }
        const std::array<std::uint16_t, VOLUME> &data() const { return mBlocks; }

        // -- Generic sidecar state (issue #3, section 5; M05 resolver) -------
        //
        // Chunk is registry-agnostic storage: sidecars are keyed by their
        // data-driven type id and hold PropertyValue. The caller (the
        // resolver in WorldState / the ChunkManager shims) supplies the
        // declared default so Chunk never hardcodes content. No per-type
        // members are added to Chunk (M04 review constraint).

        /**
         * Sets a property value for localIndex. `defaultValue` is the
         * *logical default of the object being written* (prototype-aware M05):
         * writing that value removes the override again (lazy destruction),
         * regardless of which object created the sidecar first. When the last
         * entry disappears the sidecar is dropped again.
         * @return true when the stored state actually changed.
         * @note Invariant (issue #3 section 5): sidecar state exists only
         *       while at least one block actually needs it. AIR blocks can
         *       never carry sidecar state, so writes to AIR positions are
         *       rejected here (returns false, nothing stored). */
        bool setProperty( std::uint32_t localIndex, const std::string &typeId,
                          const PropertyValue &value, const PropertyValue &defaultValue )
        {
            if( localIndex >= VOLUME )
                return false; // out-of-range local index (M09 deserialization trap)
            if( mBlocks[localIndex] == 0u )
                return false; // AIR: a sidecar entry would be zombie state
            auto it = mSidecars.find( typeId );
            if( it == mSidecars.end() )
            {
                if( value == defaultValue )
                    return false; // default on default: no state change
                it = mSidecars
                         .emplace( typeId,
                                   std::make_unique<Sidecar<PropertyValue>>( defaultValue, VOLUME ) )
                         .first;
            }
            const bool changed = it->second->setWithDefault( localIndex, value, defaultValue );
            if( it->second->empty() )
                mSidecars.erase( it ); // lazy destruction
            return changed;
        }

        /** Absent entry means the block holds its object's logical default. */
        std::optional<PropertyValue> getProperty( std::uint32_t localIndex,
                                                  const std::string &typeId ) const
        {
            const auto it = mSidecars.find( typeId );
            if( it == mSidecars.end() )
                return std::nullopt;
            return it->second->get( localIndex );
        }

        /** nullptr while the chunk holds no sidecar of the given type. */
        const Sidecar<PropertyValue> *propertySidecar( const std::string &typeId ) const
        {
            const auto it = mSidecars.find( typeId );
            return it == mSidecars.end() ? nullptr : it->second.get();
        }

        /** Drops the sidecar of the given type.
         *  @return true when a sidecar actually existed (state changed). */
        bool clearProperty( const std::string &typeId )
        {
            return mSidecars.erase( typeId ) != 0u;
        }

        /** Drops all sidecars (wholesale content replacement). */
        void clearProperties() { mSidecars.clear(); }

        /** Deterministic (type-id order) access for serialization (M09). */
        const std::map<std::string, std::unique_ptr<Sidecar<PropertyValue>>> &sidecars() const
        {
            return mSidecars;
        }

    private:
        ChunkAddress mAddress{};
        std::array<std::uint16_t, VOLUME> mBlocks{};
        std::uint32_t mNonAirCount = 0;
        // Lazy registry-driven sidecar storage: an empty chunk holds nothing.
        // std::map keeps type-id order deterministic for serialization (M09).
        std::map<std::string, std::unique_ptr<Sidecar<PropertyValue>>> mSidecars;
    };
} // namespace world
