#include "world/registry/PrototypeIdTable.h"

#include <cstdint>

namespace world
{
    namespace
    {
        constexpr std::uint32_t FNV_OFFSET = 2166136261u;
        constexpr std::uint32_t FNV_PRIME = 16777619u;
    } // namespace

    std::uint32_t PrototypeIdTable::hashId( const std::string &id )
    {
        std::uint32_t hash = FNV_OFFSET;
        for( const char c : id )
        {
            hash ^= static_cast<std::uint32_t>( static_cast<unsigned char>( c ) );
            hash *= FNV_PRIME;
        }
        return hash;
    }

    PrototypeIdTable::PrototypeIdTable( const PrototypeRegistry &prototypes )
    {
        for( const std::string &id : prototypes.ids() )
        {
            const std::uint32_t handle = hashId( id );
            const auto [it, inserted] = mHandleOfId.emplace( id, handle );
            (void)it;
            if( !inserted )
                throw RegistryError( "duplicate prototype id '" + id + "'" );

            const auto [byId, insertedById] = mById.emplace( handle, &prototypes.get( id ) );
            (void)byId;
            if( !insertedById )
                throw RegistryError( "prototype id hash collision for '" + id + "' " +
                                     "(FNV-1a handle " + std::to_string( handle ) +
                                     " is already taken; rename the id)" );
            mIds.push_back( id );
        }
    }

    std::uint32_t PrototypeIdTable::handleOf( const std::string &id ) const
    {
        const auto it = mHandleOfId.find( id );
        if( it == mHandleOfId.end() )
            throw RegistryError( "unknown prototype id '" + id + "'" );
        return it->second;
    }

    std::optional<std::uint32_t> PrototypeIdTable::tryHandleOf( const std::string &id ) const
    {
        const auto it = mHandleOfId.find( id );
        if( it == mHandleOfId.end() )
            return std::nullopt;
        return it->second;
    }

    const PrototypeDef &PrototypeIdTable::get( std::uint32_t handle ) const
    {
        const PrototypeDef *def = tryGet( handle );
        if( !def )
            throw RegistryError( "unknown prototype handle " + std::to_string( handle ) );
        return *def;
    }

    const PrototypeDef *PrototypeIdTable::tryGet( std::uint32_t handle ) const
    {
        const auto it = mById.find( handle );
        return it == mById.end() ? nullptr : it->second;
    }
} // namespace world
