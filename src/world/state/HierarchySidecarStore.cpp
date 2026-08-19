#include "world/state/HierarchySidecarStore.h"

namespace world
{
    namespace
    {
        /** Address extraction for a scope's canonical type. */
        inline const ChunkAddress *asChunk( const WorldStateTarget &target )
        {
            return std::get_if<ChunkAddress>( &target.value() );
        }
        inline const GroupAddress *asGroup( const WorldStateTarget &target )
        {
            return std::get_if<GroupAddress>( &target.value() );
        }
        inline const SectionAddress *asSection( const WorldStateTarget &target )
        {
            return std::get_if<SectionAddress>( &target.value() );
        }
        inline const RegionAddress *asRegion( const WorldStateTarget &target )
        {
            return std::get_if<RegionAddress>( &target.value() );
        }
        inline const SectorAddress *asSector( const WorldStateTarget &target )
        {
            return std::get_if<SectorAddress>( &target.value() );
        }
    } // namespace

    std::optional<PropertyValue> HierarchySidecarStore::get( const WorldStateTarget &target,
                                                             const std::string &propertyId ) const
    {
        switch( target.scope() )
        {
            case SidecarScope::Chunk:
                if( const ChunkAddress *a = asChunk( target ) )
                    return mChunks.get( *a, propertyId );
                break;
            case SidecarScope::ChunkGroup:
                if( const GroupAddress *a = asGroup( target ) )
                    return mGroups.get( *a, propertyId );
                break;
            case SidecarScope::Section:
                if( const SectionAddress *a = asSection( target ) )
                    return mSections.get( *a, propertyId );
                break;
            case SidecarScope::Region:
                if( const RegionAddress *a = asRegion( target ) )
                    return mRegions.get( *a, propertyId );
                break;
            case SidecarScope::Sector:
                if( const SectorAddress *a = asSector( target ) )
                    return mSectors.get( *a, propertyId );
                break;
            case SidecarScope::Block:
                break; // block scope is owned by the Chunk sidecar path
        }
        return std::nullopt;
    }

    bool HierarchySidecarStore::set( const WorldStateTarget &target, const std::string &propertyId,
                                     const PropertyValue &value,
                                     const PropertyValue &removalDefault )
    {
        switch( target.scope() )
        {
            case SidecarScope::Chunk:
                if( const ChunkAddress *a = asChunk( target ) )
                    return mChunks.set( *a, propertyId, value, removalDefault );
                break;
            case SidecarScope::ChunkGroup:
                if( const GroupAddress *a = asGroup( target ) )
                    return mGroups.set( *a, propertyId, value, removalDefault );
                break;
            case SidecarScope::Section:
                if( const SectionAddress *a = asSection( target ) )
                    return mSections.set( *a, propertyId, value, removalDefault );
                break;
            case SidecarScope::Region:
                if( const RegionAddress *a = asRegion( target ) )
                    return mRegions.set( *a, propertyId, value, removalDefault );
                break;
            case SidecarScope::Sector:
                if( const SectorAddress *a = asSector( target ) )
                    return mSectors.set( *a, propertyId, value, removalDefault );
                break;
            case SidecarScope::Block:
                break; // block scope is owned by the handler sidecar path
        }
        return false;
    }

    bool HierarchySidecarStore::remove( const WorldStateTarget &target,
                                        const std::string &propertyId )
    {
        switch( target.scope() )
        {
            case SidecarScope::Chunk:
                if( const ChunkAddress *a = asChunk( target ) )
                    return mChunks.remove( *a, propertyId );
                break;
            case SidecarScope::ChunkGroup:
                if( const GroupAddress *a = asGroup( target ) )
                    return mGroups.remove( *a, propertyId );
                break;
            case SidecarScope::Section:
                if( const SectionAddress *a = asSection( target ) )
                    return mSections.remove( *a, propertyId );
                break;
            case SidecarScope::Region:
                if( const RegionAddress *a = asRegion( target ) )
                    return mRegions.remove( *a, propertyId );
                break;
            case SidecarScope::Sector:
                if( const SectorAddress *a = asSector( target ) )
                    return mSectors.remove( *a, propertyId );
                break;
            case SidecarScope::Block:
                break;
        }
        return false;
    }

    std::size_t HierarchySidecarStore::addressCount( SidecarScope scope ) const
    {
        switch( scope )
        {
            case SidecarScope::Chunk: return mChunks.addressCount();
            case SidecarScope::ChunkGroup: return mGroups.addressCount();
            case SidecarScope::Section: return mSections.addressCount();
            case SidecarScope::Region: return mRegions.addressCount();
            case SidecarScope::Sector: return mSectors.addressCount();
            case SidecarScope::Block: return 0u;
        }
        return 0u;
    }

    std::size_t HierarchySidecarStore::entryCount( SidecarScope scope ) const
    {
        switch( scope )
        {
            case SidecarScope::Chunk: return mChunks.entryCount();
            case SidecarScope::ChunkGroup: return mGroups.entryCount();
            case SidecarScope::Section: return mSections.entryCount();
            case SidecarScope::Region: return mRegions.entryCount();
            case SidecarScope::Sector: return mSectors.entryCount();
            case SidecarScope::Block: return 0u;
        }
        return 0u;
    }

    std::vector<HierarchySidecarStore::EnumeratedEntry> HierarchySidecarStore::enumerate() const
    {
        std::vector<EnumeratedEntry> result;
        const auto emit = [&result]( const WorldStateTarget &target,
                                     const std::string &propertyId,
                                     const PropertyValue &value ) {
            result.push_back( EnumeratedEntry{ target, propertyId, value } );
        };
        mChunks.enumerateFor( emit );
        mGroups.enumerateFor( emit );
        mSections.enumerateFor( emit );
        mRegions.enumerateFor( emit );
        mSectors.enumerateFor( emit );
        // Scope iteration above is already in canonical scope order (the
        // scope value order); the per-scope maps iterate in address order,
        // bags in property-id order. The result is therefore fully
        // deterministic without an extra sort pass.
        return result;
    }
} // namespace world
