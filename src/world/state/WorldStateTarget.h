#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"

#include <cstdint>
#include <variant>

namespace world
{
    /**
     * Scope-aware property target of the unified world state (M01-B, #20).
     * A typed sum over the canonical address types of the full hierarchy:
     *
     *   BlockAddress | ChunkAddress | GroupAddress | SectionAddress
     *   | RegionAddress | SectorAddress
     *
     * This is the logical identity a property write is addressed at. It is
     * deliberately NOT a string, a void*, a flattened global integer or a
     * float/double: identity stays the exact canonical hierarchical address.
     * M02 reuses the same type as the base of CommunicationEnvelope.target.
     *
     * Scope association: SidecarScope of the value = index of the held
     * alternative (Block..Sector), so scope validation in WorldState/
     * Registry is a single mapping instead of six parallel APIs.
     */
    class WorldStateTarget
    {
    public:
        using Variant =
            std::variant<BlockAddress, ChunkAddress, GroupAddress, SectionAddress,
                         RegionAddress, SectorAddress>;

        // Not default-constructible (M01-B review round 4): a forgotten target must
        // never silently become a real block at the origin - especially once
        // this type becomes the basis of CommunicationEnvelope.target. Every
        // construction path carries an explicit canonical address.
        WorldStateTarget() = delete;
        explicit WorldStateTarget( const Variant &value ) : mValue( value )
        {
            // Every construction path validates the held address: no
            // non-canonical WorldStateTarget can ever exist (M01-B review).
            // No normalization, no silent carry - a non-canonical address is
            // rejected like the existing Chunk/ChunkGroup address contracts.
            std::visit( []( const auto &address ) { requireCanonical( address ); }, mValue );
        }

        // Implicit conversions so existing block-oriented call sites and the
        // M02 communication support can use the plain address types directly.
        // Each one is its own canonical gate.
        WorldStateTarget( const BlockAddress &address ) : mValue( address )
        {
            requireCanonical( address );
        }
        WorldStateTarget( const ChunkAddress &address ) : mValue( address )
        {
            requireCanonical( address );
        }
        WorldStateTarget( const GroupAddress &address ) : mValue( address )
        {
            requireCanonical( address );
        }
        WorldStateTarget( const SectionAddress &address ) : mValue( address )
        {
            requireCanonical( address );
        }
        WorldStateTarget( const RegionAddress &address ) : mValue( address )
        {
            requireCanonical( address );
        }
        WorldStateTarget( const SectorAddress &address ) : mValue( address )
        {
            requireCanonical( address ); // Sector: any int64 is canonical
        }

        const Variant &value() const { return mValue; }

        /** Target tier; matches the SidecarScope a property must declare. */
        SidecarScope scope() const
        {
            switch( mValue.index() )
            {
                case 0: return SidecarScope::Block;
                case 1: return SidecarScope::Chunk;
                case 2: return SidecarScope::ChunkGroup;
                case 3: return SidecarScope::Section;
                case 4: return SidecarScope::Region;
                default: return SidecarScope::Sector;
            }
        }

        bool isBlock() const { return std::holds_alternative<BlockAddress>( mValue ); }
        const BlockAddress &asBlock() const { return std::get<BlockAddress>( mValue ); }

        // Stable total ordering: scope first, then the canonical address.
        // std::map storage (hierarchy stores, persistence sink) iterates
        // deterministically on this order; unordered iteration order can
        // never leak into persistence/serialization.
        friend bool operator==( const WorldStateTarget &, const WorldStateTarget & ) = default;
        friend bool operator<( const WorldStateTarget &a, const WorldStateTarget &b )
        {
            if( a.mValue.index() != b.mValue.index() )
                return a.mValue.index() < b.mValue.index();
            // Same alternative: compare the canonical addresses of that type.
            return std::visit( [&b]( const auto &x ) {
                using Address = std::decay_t<decltype( x )>;
                return x < std::get<Address>( b.mValue );
            }, a.mValue );
        }

    private:
        Variant mValue;
    };
    static_assert( !std::is_default_constructible_v<WorldStateTarget> );
} // namespace world
