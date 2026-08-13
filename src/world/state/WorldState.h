#pragma once

#include "world/chunk/ChunkManager.h"
#include "world/registry/Registry.h"
#include "world/state/PersistenceSink.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace world
{
    /**
     * Unified world state (M05, issue #3): the single game-facing entry point
     * for block and block-property state.
     *
     * Hard rule: callers (Lua/game code) never know whether a value comes
     * from a prototype/sidecar default or from stored sidecar state (or, from
     * M08 on, from the ECS hot layer). get()/has()/set() hide the source.
     *
     * Resolution model:
     *  - get(): stored sidecar entry if one exists, otherwise the data-driven
     *    default declared for the property type (sidecars.json defaultValue);
     *    nullopt only for unknown property ids.
     *  - has(): true when explicit stored state exists at the position.
     *  - set(): stores through the generic per-chunk sidecar storage. Writes
     *    of the declared default remove the entry (lazy destruction), AIR
     *    positions and unknown/type-mismatched property ids are rejected and
     *    never create chunks. No-op writes report false and never notify.
     *
     * Mutations are centralised here: gameplay code must not reach into
     * ChunkManager directly (M06 action path depends on that). Worldgen base
     * load (assignBlocks) stays outside the unified path - it is not a
     * gameplay mutation.
     */
    class WorldState
    {
    public:
        WorldState( ChunkManager &chunks, const SidecarRegistry &sidecars );

        // -- unified property API --------------------------------------------
        bool has( const BlockAddress &, const std::string &propertyId ) const;
        std::optional<PropertyValue> get( const BlockAddress &,
                                          const std::string &propertyId ) const;
        /** @return true when the stored state actually changed. */
        bool set( const BlockAddress &, const std::string &propertyId, const PropertyValue &value );

        // -- central block mutation ------------------------------------------
        /** @return true when the block actually changed (no-op writes are
         *  not dirty and not persisted). */
        bool setBlock( const BlockAddress &, std::uint16_t runtimeId );
        /** Loaded block at the position, nullopt for unloaded chunks. */
        std::optional<std::uint16_t> blockAt( const BlockAddress & ) const;

        // -- dirty hooks ------------------------------------------------------
        /** Granular change hook; `what` is "block" for setBlock(), otherwise
         *  the property id. Fires only for real changes (never for no-ops). */
        using ChangeCallback = std::function<void( const BlockAddress &, const std::string &what )>;
        void setOnChange( ChangeCallback callback ) { mOnChange = std::move( callback ); }

        // -- persistence-dirty abstraction (M05, backend comes in M09) --------
        void setPersistenceSink( PersistenceSink *sink ) { mPersistenceSink = sink; }

        const SidecarRegistry &sidecars() const { return mSidecars; }

    private:
        const SidecarDef *resolve( const std::string &propertyId ) const;
        void emitChange( const BlockAddress &, const std::string &what );

        ChunkManager &mChunks;
        const SidecarRegistry &mSidecars;
        ChangeCallback mOnChange;
        PersistenceSink *mPersistenceSink = nullptr;
    };
} // namespace world
