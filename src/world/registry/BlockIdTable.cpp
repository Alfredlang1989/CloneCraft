#include "world/registry/BlockIdTable.h"

#include <limits>

namespace world
{
    BlockIdTable::BlockIdTable( const BlockRegistry &blocks )
    {
        // Zero is a structural invariant of Chunk: default-initialized memory
        // must mean air regardless of JSON ordering.
        if( !blocks.contains( "core:air" ) )
            throw RegistryError( "block registry must contain 'core:air'" );
        if( blocks.size() > static_cast<std::size_t>( std::numeric_limits<std::uint16_t>::max() ) + 1u )
            throw RegistryError( "too many blocks for uint16 runtime ids" );

        mIndex["core:air"] = 0u;
        mIds.push_back( "core:air" );

        for( const std::string &id : blocks.ids() )
        {
            if( id == "core:air" )
                continue;
            const std::uint16_t index = static_cast<std::uint16_t>( mIds.size() );
            mIndex[id] = index;
            mIds.push_back( id );
        }
    }

    std::uint16_t BlockIdTable::indexOf( const std::string &id ) const
    {
        const auto index = tryIndexOf( id );
        if( !index.has_value() )
            throw RegistryError( "unknown block id '" + id + "'" );
        return *index;
    }

    std::optional<std::uint16_t> BlockIdTable::tryIndexOf( const std::string &id ) const
    {
        const auto it = mIndex.find( id );
        if( it == mIndex.end() )
            return std::nullopt;
        return it->second;
    }

    const std::string &BlockIdTable::idOf( std::uint16_t index ) const
    {
        if( index >= mIds.size() )
            throw RegistryError( "runtime block id " + std::to_string( index ) +
                                 " is outside BlockIdTable" );
        return mIds[index];
    }
} // namespace world
