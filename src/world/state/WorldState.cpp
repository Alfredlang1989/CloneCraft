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

    bool WorldState::valueFitsSidecarDef( const SidecarDef &def, const PropertyValue &value ) const
    {
        if( def.valueType == SidecarValueType::Float )
            return std::holds_alternative<float>( value );
        if( !std::holds_alternative<std::uint32_t>( value ) )
            return false;
        const std::uint64_t v = std::get<std::uint32_t>( value );
        switch( def.valueType )
        {
            case SidecarValueType::Uint8:
                if( v > 0xFFu ) return false;
                break;
            case SidecarValueType::Uint16:
                if( v > 0xFFFFu ) return false;
                break;
            case SidecarValueType::Uint32:
                break;
            case SidecarValueType::Float:
                return false; // handled above
        }
        if( def.bitWidth != 0u )
        {
            if( def.bitWidth >= 32u )
                return true; // full uint32 range
            if( v >= ( static_cast<std::uint64_t>( 1u ) << def.bitWidth ) )
                return false; // value does not fit the declared compact width
        }
        return true;
    }

    PropertyValue WorldState::logicalDefaultFor( const PrototypeDef &prototype,
                                                 const SidecarDef &def,
                                                 const PrototypePropertyDef &decl ) const
    {
        (void)prototype;
        // Prototype-specific default when it fits the declared sidecar type;
        // otherwise fall back to the sidecar type's own default.
        if( valueFitsSidecarDef( def, decl.defaultValue ) )
            return decl.defaultValue;
        return def.defaultValue;
    }

    void WorldState::emitChange( const BlockAddress &address, const std::string &what )
    {
        if( mOnChange ) mOnChange( address, what );
    }

    bool WorldState::has( const BlockAddress &address, const std::string &propertyId ) const
    {
        const PrototypeDef *prototype = prototypeAt( address );
        return prototype != nullptr && propertyDecl( *prototype, propertyId ) != nullptr;
    }

    std::optional<PropertyValue> WorldState::get( const BlockAddress &address,
                                                  const std::string &propertyId ) const
    {
        const PrototypeDef *prototype = prototypeAt( address );
        if( !prototype )
            return std::nullopt; // AIR/unloaded/no prototype: no properties
        const PrototypePropertyDef *decl = propertyDecl( *prototype, propertyId );
        if( !decl )
            return std::nullopt; // the object does not support this property
        const SidecarDef *def = resolve( propertyId );
        if( !def )
            return std::nullopt; // unknown property id: nothing to resolve
        if( const std::optional<PropertyValue> stored =
                mChunks.blockProperty( address, propertyId ) )
            return stored;
        return logicalDefaultFor( *prototype, *def, *decl );
    }

    bool WorldState::set( const BlockAddress &address, const std::string &propertyId,
                          const PropertyValue &value )
    {
        const PrototypeDef *prototype = prototypeAt( address );
        if( !prototype )
            return false; // AIR/unloaded/no prototype: no property can be stored
        const PrototypePropertyDef *decl = propertyDecl( *prototype, propertyId );
        if( !decl )
            return false; // the object does not support this property
        const SidecarDef *def = resolve( propertyId );
        if( !def )
            return false; // unknown property id: nothing to store
        if( !valueFitsSidecarDef( *def, value ) )
            return false; // type/width mismatch: a sidecar never mixes alternatives
        const PropertyValue logicalDefault =
            logicalDefaultFor( *prototype, *def, *decl );
        const bool changed =
            mChunks.setBlockProperty( address, propertyId, value, logicalDefault );
        if( changed )
        {
            emitChange( address, propertyId );
            if( mPersistenceSink && def->persist )
                mPersistenceSink->onPropertyChanged(
                    address, propertyId,
                    value == logicalDefault ? std::nullopt
                                            : std::optional<PropertyValue>{ value } );
        }
        return changed;
    }

    bool WorldState::setBlock( const BlockAddress &address, std::uint16_t runtimeId )
    {
        // Replacing a block invalidates its sidecar state: collect the
        // property overrides at the position *before* the mutation so the
        // sink learns they no longer exist (real delta, not a bare marker).
        std::vector<std::string> removedProperties;
        if( mPersistenceSink )
        {
            for( const std::string &id : mSidecars.ids() )
                if( mChunks.blockProperty( address, id ).has_value() )
                    removedProperties.push_back( id );
        }
        const std::optional<std::uint16_t> previous = mChunks.tryBlockAt( address );
        const bool changed = mChunks.setBlock( address, runtimeId );
        if( changed )
        {
            emitChange( address, "block" );
            if( mPersistenceSink )
            {
                for( const std::string &id : removedProperties )
                    mPersistenceSink->onPropertyChanged( address, id, std::nullopt );
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