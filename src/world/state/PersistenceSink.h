#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"

#include <cstdint>
#include <optional>
#include <string>

namespace world
{
    /** Block mutation recorded for persistence (M05 abstraction, M09 sink). */
    struct BlockDelta
    {
        BlockAddress address;
        // The observed previous value and the final value. For Base+Delta
        // persistence (M09) the authoritative record is `newRuntimeId`; the
        // old id is informational (hooks/undo), not a delta pair.
        std::uint16_t oldRuntimeId = 0;
        std::uint16_t newRuntimeId = 0;
    };

    /** Property mutation recorded for persistence (M05 abstraction, M09 sink). */
    struct PropertyDelta
    {
        BlockAddress address;
        std::string propertyId;
        // Final logical value of the property: nullopt means the property
        // override no longer exists (it was reset to its prototype default
        // or the owning block was replaced/removed). A sink keeps this so
        // it knows a previous record was superseded by a removal - a real
        // delta instead of a bare dirty marker.
        std::optional<PropertyValue> value;
    };

    /**
     * Persistence-dirty abstraction (issue #3, section 5.3 / M05): gameplay
     * mutations flow through the unified world state into a PersistenceSink.
     * M09 implements the RocksDB-backed sink; game code never knows the
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
         *  no longer exists (reset to default / block replaced). */
        virtual void onPropertyChanged( const BlockAddress &, const std::string &propertyId,
                                        std::optional<PropertyValue> value ) = 0;
        virtual void flush() = 0;
    };
} // namespace world
