#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace world
{
    /**
     * Sparse per-chunk sidecar: local block index -> value.
     *
     * A sidecar exists only while at least one block in the chunk needs its
     * data (issue #3, section 5). Storage is fully lazy:
     *  - an empty Sidecar allocates nothing (empty map);
     *  - the first set()/setWithDefault() creates the entry;
     *  - writing the removal default removes the entry again, and a
     *    Sidecar that becomes empty may be destroyed entirely by its owner.
     *
     * Entries are kept in ascending local-index order (std::map) so later
     * serialization iterates deterministically.
     *
     * Removal is decided against the value supplied per call
     * (setWithDefault), not against a chunk-wide baked default, so objects
     * with different logical defaults (prototype-aware M05) can share one
     * sidecar type without losing values. set() keeps the classic
     * "own default" behaviour for the typed OrientationSidecar pilot.
     */
    template <typename T>
    class Sidecar
    {
    public:
        /**
         * @param defaultValue value that removes the entry again
         * @param capacity    number of valid local indices; 0 = unbounded.
         *                    Chunk passes its VOLUME so out-of-range indices
         *                    (a future deserialization trap) are
         *                    rejected instead of silently stored.
         */
        explicit Sidecar( T defaultValue, std::uint32_t capacity = 0u ) :
            mDefaultValue( defaultValue ), mCapacity( capacity )
        {
        }

        Sidecar( const Sidecar & ) = delete;
        Sidecar &operator=( const Sidecar & ) = delete;

        /**
         * Stores value for localIndex, removing the entry again when it
         * equals this sidecar's own default (the M04 pilot semantics). For
         * prototype-aware storage where different objects may expose the same
         * property with different logical defaults, use setWithDefault()
         * instead so the removal threshold is decided per object, not per
         * chunk (M05 review round 2).
         *
         * @return true when the stored state actually changed; false when the
         *         value already was in that state, the write was the default
         *         while no entry existed, or localIndex is outside the
         *         configured capacity.
         */
        bool set( std::uint32_t localIndex, T value )
        {
            return setWithDefault( localIndex, value, mDefaultValue );
        }

        /**
         * Prototype-aware variant: writing `removalDefault` removes the entry
         * again (lazy destruction). Two prototypes sharing one sidecar type in
         * the same chunk may pass their own logical defaults, so the removal
         * decision never depends on which object created the sidecar first
         * (write-order independence).
         *
         * @return true when the stored state actually changed; false when the
         *         value already was in that state, the write was the removal
         *         default while no entry existed, or localIndex is outside the
         *         configured capacity.
         */
        bool setWithDefault( std::uint32_t localIndex, T value, T removalDefault )
        {
            if( mCapacity != 0u && localIndex >= mCapacity )
                return false;
            if( value == removalDefault )
                return mEntries.erase( localIndex ) != 0u;
            const auto it = mEntries.find( localIndex );
            if( it != mEntries.end() && it->second == value )
                return false;
            mEntries.insert_or_assign( localIndex, value );
            return true;
        }

        /** Explicitly removes the entry for localIndex.
         *  @return true when an entry actually existed (state changed). */
        bool remove( std::uint32_t localIndex )
        {
            return mEntries.erase( localIndex ) != 0u;
        }

        /** Absent entry == default value. */
        std::optional<T> get( std::uint32_t localIndex ) const
        {
            const auto it = mEntries.find( localIndex );
            if( it == mEntries.end() )
                return std::nullopt;
            return it->second;
        }

        std::size_t entryCount() const { return mEntries.size(); }
        bool empty() const { return mEntries.empty(); }
        T defaultValue() const { return mDefaultValue; }
        std::uint32_t capacity() const { return mCapacity; }

        void clear() { mEntries.clear(); }

        /** Deterministic (ascending local index) iteration for serialization. */
        const std::map<std::uint32_t, T> &entries() const { return mEntries; }

    private:
        T mDefaultValue;
        std::uint32_t mCapacity = 0u;
        std::map<std::uint32_t, T> mEntries;
    };
} // namespace world
