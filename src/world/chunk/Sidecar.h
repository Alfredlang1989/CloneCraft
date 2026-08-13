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
     *  - the first set() creates the entry;
     *  - setting a value back to the default removes the entry again, and a
     *    Sidecar that becomes empty may be destroyed entirely by its owner.
     *
     * Entries are kept in ascending local-index order (std::map) so later
     * serialization (M09) iterates deterministically.
     */
    template <typename T>
    class Sidecar
    {
    public:
        explicit Sidecar( T defaultValue ) : mDefaultValue( defaultValue ) {}

        Sidecar( const Sidecar & ) = delete;
        Sidecar &operator=( const Sidecar & ) = delete;

        /**
         * Stores value for localIndex. Writing the default value removes the
         * entry (lazy destruction): the caller may then drop the sidecar
         * entirely when it reports empty().
         */
        void set( std::uint32_t localIndex, T value )
        {
            if( value == mDefaultValue )
            {
                mEntries.erase( localIndex );
                return;
            }
            mEntries.insert_or_assign( localIndex, value );
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

        void clear() { mEntries.clear(); }

        /** Deterministic (ascending local index) iteration for serialization. */
        const std::map<std::uint32_t, T> &entries() const { return mEntries; }

    private:
        T mDefaultValue;
        std::map<std::uint32_t, T> mEntries;
    };
} // namespace world
