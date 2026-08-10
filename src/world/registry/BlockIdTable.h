#pragma once

#include "world/registry/Registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace world
{
    /**
     * Maps registered block ids to compact uint16 indices (the payload
     * stored in Chunk voxel buffers). Air is always index 0, so a zeroed
     * buffer equals an all-air chunk. Built from the BlockRegistry in
     * its insertion order (see ids()).
     */
    class BlockIdTable
    {
    public:
        BlockIdTable() = default;
        explicit BlockIdTable( const BlockRegistry &blocks );

        /** Strict lookup. Unknown ids are configuration/programming errors. */
        std::uint16_t indexOf( const std::string &id ) const;

        /** Non-throwing lookup for callers that explicitly need probing. */
        std::optional<std::uint16_t> tryIndexOf( const std::string &id ) const;

        /** Strict reverse lookup. Out-of-range ids indicate corrupt voxel data. */
        const std::string &idOf( std::uint16_t index ) const;

        std::size_t size() const { return mIds.size(); }

    private:
        std::unordered_map<std::string, std::uint16_t> mIndex;
        std::vector<std::string> mIds;
    };
} // namespace world