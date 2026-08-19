#include "world/state/WorldState.h"

#include <cstdint>
#include <vector>

namespace world
{
    WorldState::WorldState( ChunkManager &chunks, const BlockIdTable &idTable,
                            const SidecarRegistry &sidecars,
                            const PrototypeRegistry &prototypes ) :
        mChunks( chunks ), mIdTable( idTable ), mSidecars( sidecars ), mPrototypes( prototypes )
    {
    }

    const SidecarDef *WorldState::resolve( const std::string &propertyId ) const
    {
        return mSidecars.find( propertyId );
    }

    const PrototypeDef *WorldState::prototypeAt( const BlockAddress &address ) const
    {
        const std::optional<std::uint16_t> runtimeId = mChunks.tryBlockAt( address );
        if( !runtimeId || *runtimeId == 0u )
            return nullptr; // unloaded or AIR: no object owns properties
        return world::prototypeForBlock( mPrototypes, mIdTable.idOf( *runtimeId ) );
    }

    const PrototypePropertyDef *WorldState::propertyDecl( const PrototypeDef &prototype,
                                                          const std::string &propertyId ) const
    {
        for( const PrototypePropertyDef &decl : prototype.properties )
            if( decl.id == propertyId )
                return &decl;
        return nullptr;
    }

    PropertyValue WorldState::logicalDefaultFor( const PrototypeDef &prototype,
                                                 const SidecarDef &def,
                                                 const PrototypePropertyDef &decl ) const
    {
        (void)prototype;
        // Prototype-specific default when it fits the declared sidecar type;
        // otherwise fall back to the sidecar type's own default.
        if( world::valueFitsSidecarDef( def, decl.defaultValue ) )
            return decl.defaultValue;
        return def.defaultValue;
    }

    void WorldState::emitChange( const BlockAddress &address, const std::string &what )
    {
        // Legacy block-scope hook: kept so renderer/old consumers keep
        // working unchanged (M01-B review finding 2). The generic target
        // hook below is the single change language for every scope.
        if( mOnChange ) mOnChange( address, what );
    }

    void WorldState::emitTargetChange( const WorldStateTarget &target, const std::string &what )
    {
        if( mOnTargetChange ) mOnTargetChange( target, what );
    }

    /** Every real property/block change funnels through one generic target
     *  hook; the block scope additionally keeps the legacy hook. */
    void WorldState::emitAnyChange( const WorldStateTarget &target, const std::string &what )
    {
        emitTargetChange( target, what );
        if( target.isBlock() )
            emitChange( target.asBlock(), what );
    }

    // -- block scope (prototype-aware, M05) ----------------------------------

    bool WorldState::hasBlock( const BlockAddress &address, const std::string &propertyId,
                               const SidecarDef * ) const
    {
        const PrototypeDef *prototype = prototypeAt( address );
        if( !prototype )
            return false;
        return propertyDecl( *prototype, propertyId ) != nullptr;
    }

    std::optional<PropertyValue> WorldState::getBlock( const BlockAddress &address,
                                                       const std::string &propertyId,
                                                       const SidecarDef *def ) const
    {
        const PrototypeDef *prototype = prototypeAt( address );
        if( !prototype )
            return std::nullopt; // AIR/unloaded/no prototype: no properties
        const PrototypePropertyDef *decl = propertyDecl( *prototype, propertyId );
        if( !decl )
            return std::nullopt; // the object does not support this property
        if( const std::optional<PropertyValue> stored =
                mChunks.blockProperty( address, propertyId ) )
            return stored;
        return logicalDefaultFor( *prototype, *def, *decl );
    }

    bool WorldState::setBlock( const BlockAddress &address, const std::string &propertyId,
                               const SidecarDef *def, const PropertyValue &value )
    {
        const PrototypeDef *prototype = prototypeAt( address );
        if( !prototype )
            return false; // AIR/unloaded/no prototype: no property can be stored
        const PrototypePropertyDef *decl = propertyDecl( *prototype, propertyId );
        if( !decl )
            return false; // the object does not support this property
        const PropertyValue logicalDefault =
            logicalDefaultFor( *prototype, *def, *decl );
        const bool changed =
            mChunks.setBlockProperty( address, propertyId, value, logicalDefault );
        if( changed )
        {
            // One change language: generic target hook plus the legacy block
            // hook (renderer compatibility).
            emitAnyChange( WorldStateTarget{ address }, propertyId );
            if( mPersistenceSink && def->persist )
                mPersistenceSink->onPropertyChanged(
                    WorldStateTarget{ address }, propertyId,
                    value == logicalDefault ? std::nullopt
                                            : std::optional<PropertyValue>{ value } );
        }
        return changed;
    }

    // -- scope-aware API (M01-B, #20) ----------------------------------------

    bool WorldState::has( const WorldStateTarget &target,
                          const std::string &propertyId ) const
    {
        const SidecarDef *def = resolve( propertyId );
        // Unknown id or an id registered for a different scope: no logical
        // capability at this target (a block property can never exist on a
        // region and vice versa).
        if( !def || def->scope != target.scope() )
            return false;
        if( target.isBlock() )
            return hasBlock( target.asBlock(), propertyId, def );
        // Chunk and higher tiers: the registered declaration is the
        // capability; no prototype inheritance exists in M01-B.
        return true;
    }

    std::optional<PropertyValue> WorldState::get( const WorldStateTarget &target,
                                                  const std::string &propertyId ) const
    {
        const SidecarDef *def = resolve( propertyId );
        if( !def || def->scope != target.scope() )
            return std::nullopt;
        if( target.isBlock() )
            return getBlock( target.asBlock(), propertyId, def );
        if( const std::optional<PropertyValue> stored =
                mHierarchyStore.get( target, propertyId ) )
            return stored;
        return def->defaultValue; // stored override -> SidecarDef default
    }

    bool WorldState::set( const WorldStateTarget &target, const std::string &propertyId,
                          const PropertyValue &value )
    {
        const SidecarDef *def = resolve( propertyId );
        if( !def || def->scope != target.scope() )
            return false; // unknown id or foreign scope: reject
        if( !world::valueFitsSidecarDef( *def, value ) )
            return false; // type/width mismatch: a sidecar never mixes alternatives
        if( target.isBlock() )
            return setBlock( target.asBlock(), propertyId, def, value );
        // Hierarchy scope: sparse metadata at the canonical address, no
        // object materialization, removal decided against the registered
        // SidecarDef default (no prototype inheritance in M01-B).
        const bool changed =
            mHierarchyStore.set( target, propertyId, value, def->defaultValue );
        if( changed )
        {
            // One mutation funnel: emitAnyChange dispatches only the generic
            // target hook for non-Block scopes (and additionally the legacy
            // block hook for Block targets), so every property/block mutation
            // in the world state shares a single change path.
            emitAnyChange( target, propertyId );
            if( mPersistenceSink && def->persist )
                mPersistenceSink->onPropertyChanged(
                    target, propertyId,
                    value == def->defaultValue ? std::nullopt
                                               : std::optional<PropertyValue>{ value } );
        }
        return changed;
    }

    // -- block-scope convenience overloads (delegate to the target API) ------

    bool WorldState::has( const BlockAddress &address, const std::string &propertyId ) const
    {
        return has( WorldStateTarget{ address }, propertyId );
    }

    std::optional<PropertyValue> WorldState::get( const BlockAddress &address,
                                                  const std::string &propertyId ) const
    {
        return get( WorldStateTarget{ address }, propertyId );
    }

    bool WorldState::set( const BlockAddress &address, const std::string &propertyId,
                          const PropertyValue &value )
    {
        return set( WorldStateTarget{ address }, propertyId, value );
    }

    bool WorldState::setBlock( const BlockAddress &address, std::uint16_t runtimeId )
    {
        // The central mutation must never store corrupt voxel data: a runtime
        // id outside the BlockIdTable would later explode in reverse lookups
        // (M05 round 2).
        if( runtimeId >= mIdTable.size() )
            return false;
        // Replacing a block invalidates its sidecar state: collect the
        // property overrides at the position *before* the mutation so the
        // sink learns they no longer exist (real delta, not a bare marker).
        // Only persistable sidecar types reach the sink (persist: false is
        // filtered here as well, not just on the normal set() path).
        std::vector<std::string> removedProperties;
        if( mPersistenceSink )
        {
            for( const std::string &id : mSidecars.ids() )
            {
                const SidecarDef *def = mSidecars.find( id );
                if( def && def->persist && mChunks.blockProperty( address, id ).has_value() )
                    removedProperties.push_back( id );
            }
        }
        const std::optional<std::uint16_t> previous = mChunks.tryBlockAt( address );
        const bool changed = mChunks.setBlock( address, runtimeId );
        if( changed )
        {
            // Block replacement speaks the generic target language too
            // (what = "block"); the legacy block hook stays compatible.
            emitAnyChange( WorldStateTarget{ address }, "block" );
            if( mPersistenceSink )
            {
                for( const std::string &id : removedProperties )
                    mPersistenceSink->onPropertyChanged( WorldStateTarget{ address }, id, std::nullopt );
                mPersistenceSink->onBlockChanged( address, previous.value_or( 0u ), runtimeId );
            }
        }
        return changed;
    }

    std::optional<std::uint16_t> WorldState::blockAt( const BlockAddress &address ) const
    {
        return mChunks.tryBlockAt( address );
    }
} // namespace world
