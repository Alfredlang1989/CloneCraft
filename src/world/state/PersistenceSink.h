#pragma once

#include "world/coordinates/Coords.h"

#include <cstdint>
#include <string>

namespace world
{
    /** Block mutation recorded for persistence (M05 abstraction, M09 sink). */
    struct BlockDelta
    {
        BlockAddress address;
        std::uint16_t oldRuntimeId = 0;
        std::uint16_t newRuntimeId = 0;
    };

    /** Property mutation recorded for persistence (M05 abstraction, M09 sink). */
    struct PropertyDelta
    {
        BlockAddress address;
        std::string propertyId;
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
        virtual void onPropertyChanged( const BlockAddress &, const std::string &propertyId ) = 0;
        virtual void flush() = 0;
    };
} // namespace world
