#pragma once

#include "world/registry/Registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace world
{
    /**
     * Maps registered prototype ids to stable 32-bit runtime handles.
     *
     * The handle is a deterministic hash of the namespaced id (FNV-1a),
     * so it is *independent of load order and insertion order*: two
     * content roots loaded in different orders always produce the same
     * handle for the same id. Collisions are detected and rejected at
     * construction time (a hash collision is a configuration error).
     */
    class PrototypeIdTable
    {
    public:
        PrototypeIdTable() = default;
        explicit PrototypeIdTable( const PrototypeRegistry &prototypes );

        /** Stable hash handle for a namespaced prototype id. */
        static std::uint32_t hashId( const std::string &id );

        /** Strict lookup. Unknown ids are configuration/programming errors. */
        std::uint32_t handleOf( const std::string &id ) const;

        /** Non-throwing lookup for callers that explicitly need probing. */
        std::optional<std::uint32_t> tryHandleOf( const std::string &id ) const;

        /** Strict reverse lookup. Unknown handles are corrupt data. */
        const PrototypeDef &get( std::uint32_t handle ) const;

        /** Non-throwing reverse lookup. */
        const PrototypeDef *tryGet( std::uint32_t handle ) const;

        std::size_t size() const { return mIds.size(); }

    private:
        std::unordered_map<std::string, std::uint32_t> mHandleOfId;
        std::unordered_map<std::uint32_t, const PrototypeDef *> mById;
        std::vector<std::string> mIds;
    };
} // namespace world
