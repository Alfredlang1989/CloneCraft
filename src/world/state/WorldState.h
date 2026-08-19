#pragma once

#include "world/chunk/ChunkManager.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/ObjectRef.h"
#include "world/registry/Registry.h"
#include "world/state/HierarchySidecarStore.h"
#include "world/state/PersistenceSink.h"
#include "world/state/WorldStateTarget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace world
{
    /**
     * Unified world state (M05, issue #3): the single game-facing entry point
     * for block and block-property state.
     *
     * Hard rule: callers (Lua/game code) never know whether a value comes
     * from a prototype default, stored sidecar state or (from M08 on) the ECS
     * hot layer. get()/has()/set() hide the source.
     *
     * The world state is prototype-aware (the M05 review gate): a property
     * exists for an object only when the object's prototype declares it in
     * `prototype.properties` (prototypes.json) AND that property id resolves
     * to a registered sidecar type (sidecars.json). Plain scenery blocks
     * without a prototype, AIR and unloaded chunks own no properties:
     *  - has(): "does this object support property X" (logical capability) -
     *    true exactly when the block's prototype declares X and the id maps
     *    to a registered sidecar type, independent of stored state.
     *  - get(): the stored override if one exists, otherwise the
     *    prototype-specific default, otherwise the sidecar type default;
     *    nullopt when the object does not declare the property.
     *  - set(): stores a per-block override of the prototype default. It is
     *    rejected when the object does not declare the property, when the id
     *    is unknown, when the value does not fit the declared sidecar type
     *    (including bitWidth) or on AIR/unloaded positions - and never
     *    creates chunks. Writing the logical default removes the override.
     *
     * Removal is prototype-aware: two prototypes may share one sidecar type
     * with different logical defaults in the same chunk. The sidecar's
     * "remove the override again" decision is taken against the object's own
     * logical default per write, never against a chunk-wide baked default, so
     * world-state behaviour is independent of which object first created the
     * sidecar (write-order independence, M05 review round 2).
     *
     * Mutations are centralised here: gameplay code must not reach into
     * ChunkManager directly (M06 action path depends on that). Worldgen base
     * load (assignBlocks) stays outside the unified path - it is not a
     * gameplay mutation.
     */
    class WorldState
    {
    public:
        WorldState( ChunkManager &chunks, const BlockIdTable &idTable,
                    const SidecarRegistry &sidecars, const PrototypeRegistry &prototypes );

        // -- unified property API --------------------------------------------
        bool has( const BlockAddress &, const std::string &propertyId ) const;
        std::optional<PropertyValue> get( const BlockAddress &,
                                          const std::string &propertyId ) const;
        /** @return true when the stored state actually changed. */
        bool set( const BlockAddress &, const std::string &propertyId, const PropertyValue &value );

        // -- scope-aware property API (M01-B, #20) ----------------------------
        // One logical resolver contract for every hierarchy tier:
        // Block | Chunk | ChunkGroup | Section | Region | Sector. The target
        // scope must equal the registered SidecarDef.scope, otherwise the
        // operation is rejected (a block property can never be written to a
        // region and vice versa). Block references preserve the full
        // prototype-aware semantics; Chunk and higher tiers resolve
        // stored override -> SidecarDef default (no prototype inheritance in
        // M01-B, no implicit parent->child). Hierarchy writes never
        // materialize chunks/groups/sections/regions/sectors - only sparse
        // metadata is stored at the canonical address.
        bool has( const WorldStateTarget &, const std::string &propertyId ) const;
        std::optional<PropertyValue> get( const WorldStateTarget &,
                                          const std::string &propertyId ) const;
        /** @return true when the stored state actually changed. */
        bool set( const WorldStateTarget &, const std::string &propertyId,
                  const PropertyValue &value );

        // -- central block mutation ------------------------------------------
        /** @return true when the block actually changed (no-op writes are
         *  not dirty and not persisted). Replacing a block invalidates its
         *  property overrides and reports their removal to the sink.
         *  Invalid runtime block ids (outside the BlockIdTable) and AIR
         *  writes to unloaded positions are rejected: this central mutation
         *  never stores corrupt voxel data and never materializes a chunk
         *  for a vacuous write. */
        bool setBlock( const BlockAddress &, std::uint16_t runtimeId );
        /** Loaded block at the position, nullopt for unloaded chunks. */
        std::optional<std::uint16_t> blockAt( const BlockAddress & ) const;

        // -- dirty hooks ------------------------------------------------------
        /** Granular change hook; `what` is "block" for setBlock(), otherwise
         *  the property id. Fires only for real changes (never for no-ops).
         *  Block-scope path. */
        using ChangeCallback = std::function<void( const BlockAddress &, const std::string &what )>;
        void setOnChange( ChangeCallback callback ) { mOnChange = std::move( callback ); }

        /** Scope-aware change hook (M01-B): fires for real hierarchy-scope
         *  property changes (Chunk .. Sector) and delivers the canonical
         *  target + property id. Consumers (renderer/simulation/streaming)
         *  decide which scope/property changes interest them - a Region
         *  property change never triggers a mesh rebuild implicitly. */
        using TargetChangeCallback =
            std::function<void( const WorldStateTarget &, const std::string &what )>;
        void setOnTargetChange( TargetChangeCallback callback )
        {
            mOnTargetChange = std::move( callback );
        }

        // -- persistence-dirty abstraction (M05 contract) ---------------------
        void setPersistenceSink( PersistenceSink *sink ) { mPersistenceSink = sink; }

        const SidecarRegistry &sidecars() const { return mSidecars; }
        const PrototypeRegistry &prototypes() const { return mPrototypes; }

    private:
        const SidecarDef *resolve( const std::string &propertyId ) const;
        const PrototypeDef *prototypeAt( const BlockAddress & ) const;
        const PrototypePropertyDef *propertyDecl( const PrototypeDef &,
                                                  const std::string &propertyId ) const;
        PropertyValue logicalDefaultFor( const PrototypeDef &, const SidecarDef &,
                                         const PrototypePropertyDef & ) const;
        void emitChange( const BlockAddress &, const std::string &what );
        void emitTargetChange( const WorldStateTarget &, const std::string &what );
        /** Generic change language: always fires the target hook, and for
         *  block scope additionally the legacy block hook. */
        void emitAnyChange( const WorldStateTarget &, const std::string &what );
        /** Block-scope capability gate (prototype-aware, M05). */
        bool hasBlock( const BlockAddress &, const std::string &propertyId,
                       const SidecarDef * ) const;
        std::optional<PropertyValue> getBlock( const BlockAddress &,
                                               const std::string &propertyId,
                                               const SidecarDef * ) const;
        bool setBlock( const BlockAddress &, const std::string &propertyId,
                       const SidecarDef *, const PropertyValue &value );

        ChunkManager &mChunks;
        const BlockIdTable &mIdTable;
        const SidecarRegistry &mSidecars;
        const PrototypeRegistry &mPrototypes;
        ChangeCallback mOnChange;
        TargetChangeCallback mOnTargetChange;
        PersistenceSink *mPersistenceSink = nullptr;
        /** Sparse scope-aware storage for Chunk .. Sector (M01-B #20).
         *  World-data layer; the Block tier continues to live in the Chunk
         *  sidecars (prototype-aware, untouched). */
        HierarchySidecarStore mHierarchyStore;
    };
} // namespace world
