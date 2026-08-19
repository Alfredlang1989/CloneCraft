#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"
#include "world/state/WorldStateTarget.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace world
{
    /**
     * Generic sparse sidecar store for the hierarchy tiers above Block
     * (M01-B, #20): Chunk, ChunkGroup, Section, Region, Sector.
     *
     * One generic implementation, instantiated once per canonical address
     * type - not six independent systems. Storage is:
     *
     *   scope map: canonical address -> PropertyBag
     *   PropertyBag: property id -> PropertyValue (overrides only)
     *
     * Invariants:
     *  - a bag contains only actual non-default overrides;
     *  - writing the removal default erases the entry, an empty bag is
     *    erased, an empty address entry is erased (lazy allocation both
     *    directions);
     *  - values exist ONLY when explicitly written: the caller resolves the
     *    logical default (SidecarDef default in M01-B; prototypes for Block
     *    scope live elsewhere and are untouched);
     *  - std::map everywhere: enumeration and serialization order are
     *    deterministic (sorted by canonical address and property id) and can
     *    never depend on unordered_map iteration;
     *  - no hierarchy object is materialized: writing a Section/Region/
     *    Sector property never creates ChunkGroups, Chunks or higher
     *    containers - the store only holds the address + value, and the
     *    WorldState layer owns no ChunkManager coupling for these scopes.
     */
    class HierarchySidecarStore
    {
    public:
        /** Stored override for the target, nullopt when absent. */
        std::optional<PropertyValue> get( const WorldStateTarget &target,
                                          const std::string &propertyId ) const;

        /**
         * Writes an override for target/propertyId. Writing the removal
         * default erases the entry (lazy destruction); empty bags/addresses
         * are erased. The removal decision is taken against the supplied
         * default - the logical default the caller resolved for this target
         * (write-order independent, mirrors the block-side Sidecar::set).
         * @return true when the stored state actually changed.
         */
        bool set( const WorldStateTarget &target, const std::string &propertyId,
                  const PropertyValue &value, const PropertyValue &removalDefault );

        /** Explicitly removes the override. @return true when it existed. */
        bool remove( const WorldStateTarget &target, const std::string &propertyId );

        std::size_t addressCount( SidecarScope scope ) const;
        std::size_t entryCount( SidecarScope scope ) const;

        /** Deterministic enumeration (ascending scope, address, property id);
         *  the seed of future serialization. */
        struct EnumeratedEntry
        {
            WorldStateTarget target;
            std::string propertyId;
            PropertyValue value;
            friend bool operator==( const EnumeratedEntry &, const EnumeratedEntry & ) = default;
        };
        std::vector<EnumeratedEntry> enumerate() const;

    private:
        template <typename Address>
        struct PropertyBag
        {
            std::map<std::string, PropertyValue> values; // ascending id order
        };

        template <typename Address>
        struct SparseStore
        {
            std::map<Address, PropertyBag<Address>> entries; // ascending address order

            std::optional<PropertyValue> get( const Address &address,
                                              const std::string &propertyId ) const
            {
                const auto addressIt = entries.find( address );
                if( addressIt == entries.end() )
                    return std::nullopt;
                const auto valueIt = addressIt->second.values.find( propertyId );
                if( valueIt == addressIt->second.values.end() )
                    return std::nullopt;
                return valueIt->second;
            }

            bool set( const Address &address, const std::string &propertyId,
                      const PropertyValue &value, const PropertyValue &removalDefault )
            {
                // Removal default: never create the address entry. Look it up
                // first; an untouched address is a pure no-op (no transient
                // allocation).
                if( value == removalDefault )
                {
                    const auto addressIt = entries.find( address );
                    if( addressIt == entries.end() )
                        return false;
                    const bool existed =
                        addressIt->second.values.erase( propertyId ) != 0u;
                    if( addressIt->second.values.empty() )
                        entries.erase( addressIt );
                    return existed;
                }
                // Non-default: create the bag in place (try_emplace), then
                // compare before write to report real changes only.
                auto &bag = entries.try_emplace( address ).first->second;
                const auto valueIt = bag.values.find( propertyId );
                if( valueIt != bag.values.end() && valueIt->second == value )
                    return false;
                bag.values.insert_or_assign( propertyId, value );
                return true;
            }

            bool remove( const Address &address, const std::string &propertyId )
            {
                const auto addressIt = entries.find( address );
                if( addressIt == entries.end() )
                    return false;
                const bool existed = addressIt->second.values.erase( propertyId ) != 0u;
                if( addressIt->second.values.empty() )
                    entries.erase( addressIt );
                return existed;
            }

            std::size_t addressCount() const { return entries.size(); }

            std::size_t entryCount() const
            {
                std::size_t total = 0;
                for( const auto &[address, bag] : entries )
                {
                    (void)address;
                    total += bag.values.size();
                }
                return total;
            }

template <typename Emit>
            void enumerateFor( Emit &&emit ) const
            {
                for( const auto &[address, bag] : entries )
                {
                    (void)address;
                    for( const auto &[propertyId, value] : bag.values )
                        emit( WorldStateTarget( address ), propertyId, value );
                }
            }
        };

        SparseStore<ChunkAddress> mChunks;
        SparseStore<GroupAddress> mGroups;
        SparseStore<SectionAddress> mSections;
        SparseStore<RegionAddress> mRegions;
        SparseStore<SectorAddress> mSectors;
    };
} // namespace world
