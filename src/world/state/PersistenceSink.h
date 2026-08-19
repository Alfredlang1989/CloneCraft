#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"
#include "world/state/WorldStateTarget.h"

#include <cstdint>
#include <optional>
#include <string>

namespace world
{
    /** Block mutation recorded for persistence (M05 abstraction and sink). */
    struct BlockDelta
    {
        BlockAddress address;
        // The observed previous value and the final value. For Base+Delta
        // Base+Delta persistence the authoritative record is `newRuntimeId`; the
        // old id is informational (hooks/undo), not a delta pair.
        std::uint16_t oldRuntimeId = 0;
        std::uint16_t newRuntimeId = 0;
    };

    /** Property mutation recorded for persistence (M05 abstraction and sink).
     *
     * M01-B (#20): the logical delta identity is scope-aware and carries no
     * redundancy. `target` is the full canonical WorldStateTarget
     * (BlockAddress .. SectorAddress); target.scope() states the tier the
     * property belongs to, so a Chunk, ChunkGroup, Section, Region or Sector
     * property is a first-class persistent identity and never a fake block
     * index. There is deliberately NO BlockAddress member: a Region/Sector
     * delta holds only its real identity, so later serialization can never
     * choose between two conflicting truths.
     */
    struct PropertyDelta
    {
        WorldStateTarget target;
        std::string propertyId;
        // Final logical value of the property: nullopt means the property
        // override no longer exists (it was reset to a default, explicitly
        // removed, or the owning block was replaced). A sink keeps this so
        // it knows a previous record was superseded by a removal - a real
        // delta instead of a bare dirty marker.
        std::optional<PropertyValue> value;

        /** Legacy convenience: the BlockAddress for block-scope deltas. */
        std::optional<BlockAddress> blockAddress() const
        {
            if( !target.isBlock() )
                return std::nullopt;
            return target.asBlock();
        }
    };

    /**
     * Persistence-dirty abstraction (issue #3, section 5.3 / M05): gameplay
     * mutations flow through the unified world state into a PersistenceSink.
     * M05 implements the RocksDB-backed sink; game code never knows the
     * backend. Sinks must accept writes on any thread of the caller and
     * report flush() as the durable point.
     */
    class PersistenceSink
    {
    public:
        virtual ~PersistenceSink() = default;

        virtual void onBlockChanged( const BlockAddress &, std::uint16_t oldRuntimeId,
                                     std::uint16_t newRuntimeId ) = 0;
        /** @param value final property value, or nullopt when the override
         *  no longer exists (reset to default / removed / owning block
         *  replaced). The target carries the scope: block-scope properties
         *  come as BlockAddress; higher scopes use the canonical address of
         *  their tier. */
        virtual void onPropertyChanged( const WorldStateTarget &,
                                        const std::string &propertyId,
                                        std::optional<PropertyValue> value ) = 0;
        virtual void flush() = 0;
    };
} // namespace world
