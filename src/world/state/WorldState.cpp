#include "world/state/WorldState.h"

namespace world
{
    WorldState::WorldState( ChunkManager &chunks, const SidecarRegistry &sidecars ) :
        mChunks( chunks ), mSidecars( sidecars )
    {
    }

    const SidecarDef *WorldState::resolve( const std::string &propertyId ) const
    {
        return mSidecars.find( propertyId );
    }

    void WorldState::emitChange( const BlockAddress &address, const std::string &what )
    {
        if( mOnChange ) mOnChange( address, what );
    }

    bool WorldState::has( const BlockAddress &address, const std::string &propertyId ) const
    {
        return mChunks.blockProperty( address, propertyId ).has_value();
    }

    std::optional<PropertyValue> WorldState::get( const BlockAddress &address,
                                                  const std::string &propertyId ) const
    {
        const SidecarDef *def = resolve( propertyId );
        if( !def )
            return std::nullopt; // unknown property id: nothing to resolve
        if( const std::optional<PropertyValue> stored =
                mChunks.blockProperty( address, propertyId ) )
            return stored;
        return def->defaultValue; // no stored state: data-driven default
    }

    bool WorldState::set( const BlockAddress &address, const std::string &propertyId,
                          const PropertyValue &value )
    {
        const SidecarDef *def = resolve( propertyId );
        if( !def )
            return false; // unknown property id: nothing to store
        const bool isFloat = std::holds_alternative<float>( value );
        if( ( def->valueType == SidecarValueType::Float ) != isFloat )
            return false; // type mismatch: a sidecar never mixes alternatives
        const bool changed = mChunks.setBlockProperty( address, propertyId, value,
                                                       def->defaultValue );
        if( changed )
        {
            emitChange( address, propertyId );
            if( mPersistenceSink )
                mPersistenceSink->onPropertyChanged( address, propertyId );
        }
        return changed;
    }

    bool WorldState::setBlock( const BlockAddress &address, std::uint16_t runtimeId )
    {
        const std::optional<std::uint16_t> previous = mChunks.tryBlockAt( address );
        const bool changed = mChunks.setBlock( address, runtimeId );
        if( changed )
        {
            emitChange( address, "block" );
            if( mPersistenceSink )
                mPersistenceSink->onBlockChanged( address, previous.value_or( 0u ), runtimeId );
        }
        return changed;
    }

    std::optional<std::uint16_t> WorldState::blockAt( const BlockAddress &address ) const
    {
        return mChunks.tryBlockAt( address );
    }
} // namespace world
