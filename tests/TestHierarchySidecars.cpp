#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/state/HierarchySidecarStore.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"
#include "world/state/WorldStateTarget.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace world;

    // ---------------------------------------------------------------- corpus
    // Canonical addresses of the test corpus. Region/Section digits are
    // within their logical radices; group/chunk digits within the physical
    // radices. No flattened coordinates anywhere.
    const SectorAddress SECTOR_A{ SectorCoord{ 3, 0, 0 } };
    const SectorAddress SECTOR_B{ SectorCoord{ 21, 0, 0 } };
    const RegionAddress REGION_A{ SectorCoord{ 3, 0, 0 }, LocalRegionCoord{ 9, 0, 0 } };
    const RegionAddress REGION_B{ SectorCoord{ 21, 0, 0 }, LocalRegionCoord{ 9, 0, 0 } }; // same local, other sector
    const SectionAddress SECTION_A{ { 3, 0, 0 }, { 9, 0, 0 }, { 7, 0, 0 } };
    const SectionAddress SECTION_B{ { 21, 0, 0 }, { 9, 0, 0 }, { 7, 0, 0 } };   // same locals, other sector
    const SectionAddress SECTION_C{ { 3, 0, 0 }, { 8, 0, 0 }, { 7, 0, 0 } };   // same locals, other region
    const GroupAddress GROUP_A{ { 3, 0, 0 }, { 9, 0, 0 }, { 7, 0, 0 }, { 2, 0, 0 } };
    const ChunkAddress CHUNK_A{ GROUP_A, { 5, 0, 0 } };

    // Second, distinct addresses per hierarchy scope (isolation probes).
    const ChunkAddress CHUNK_B{ GROUP_A, { 9, 0, 0 } };
    const GroupAddress GROUP_B{ { 3, 0, 0 }, { 9, 0, 0 }, { 7, 0, 0 }, { 6, 0, 0 } };

    const std::array<SidecarScope, 6> SIX_SCOPES = {
        SidecarScope::Block,   SidecarScope::Chunk,    SidecarScope::ChunkGroup,
        SidecarScope::Section, SidecarScope::Region,   SidecarScope::Sector
    };

    std::string scopeId( SidecarScope scope )
    {
        switch( scope )
        {
            case SidecarScope::Block: return "test:block_state";
            case SidecarScope::Chunk: return "test:chunk_state";
            case SidecarScope::ChunkGroup: return "test:group_state";
            case SidecarScope::Section: return "test:section_state";
            case SidecarScope::Region: return "test:region_state";
            case SidecarScope::Sector: return "test:sector_state";
        }
        return "test:unknown_state";
    }

    PropertyValue V( std::uint32_t value ) { return { value }; }

    WorldStateTarget targetFor( SidecarScope scope )
    {
        switch( scope )
        {
            case SidecarScope::Block: return WorldStateTarget( BlockAddress{} );
            case SidecarScope::Chunk: return WorldStateTarget( CHUNK_A );
            case SidecarScope::ChunkGroup: return WorldStateTarget( GROUP_A );
            case SidecarScope::Section: return WorldStateTarget( SECTION_A );
            case SidecarScope::Region: return WorldStateTarget( REGION_A );
            case SidecarScope::Sector: return WorldStateTarget( SECTOR_A );
        }
        return WorldStateTarget( CHUNK_A );
    }

    // -------------------------------------------------------------- fixture
    struct Fixture
    {
        BlockRegistry blocks;
        BlockIdTable idTable;
        SidecarRegistry sidecars;
        PrototypeRegistry prototypes;
        ChunkManager chunks;
        std::unique_ptr<WorldState> state;
        MemoryPersistenceSink sink;

        std::uint16_t orientedId = 0;
        BlockAddress block;

        /** @param loadPilotBlock setBlock() loads the pilot block (block-scope
         *  checks); pass false when a pristine chunk-less world is needed. */
        explicit Fixture( bool loadPilotBlock = true )
        {
            BlockDef air;
            air.id = "core:air";
            air.displayName = "Air";
            blocks.insert( air );
            BlockDef oriented;
            oriented.id = "test:oriented";
            oriented.displayName = "Oriented";
            blocks.insert( oriented );
            idTable = BlockIdTable( blocks );
            orientedId = idTable.indexOf( "test:oriented" );

            for( SidecarScope scope : SIX_SCOPES )
            {
                SidecarDef def;
                def.id = scopeId( scope );
                def.displayName = def.id;
                def.valueType = SidecarValueType::Uint32;
                def.scope = scope;
                def.defaultValue = 0u;
                sidecars.insert( def );
            }

            // Region fixtures: a non-zero registered default and persist:false.
            SidecarDef default7;
            default7.id = "test:region_default7";
            default7.displayName = "Region Default 7";
            default7.valueType = SidecarValueType::Uint32;
            default7.scope = SidecarScope::Region;
            default7.defaultValue = 7u;
            sidecars.insert( default7 );

            // A float-typed region property (round 6: finite-by-contract).
            SidecarDef regionFloat;
            regionFloat.id = "test:region_float";
            regionFloat.displayName = "Region Float";
            regionFloat.valueType = SidecarValueType::Float;
            regionFloat.scope = SidecarScope::Region;
            regionFloat.defaultValue = 0.0f;
            sidecars.insert( regionFloat );

            SidecarDef noPersist;
            noPersist.id = "test:no_persist";
            noPersist.displayName = "No Persist";
            noPersist.valueType = SidecarValueType::Uint32;
            noPersist.scope = SidecarScope::Region;
            noPersist.defaultValue = 0u;
            noPersist.persist = false;
            sidecars.insert( noPersist );

            // Prototype for the block-scope path: test:oriented declares
            // test:block_state (block scope, gate-validated at load time).
            RegistryLoader::parsePrototypes(
                nlohmann::json::parse( std::string( R"({"prototypes":[{"id":"test:oriented",)"
                                          R"("displayName":"Oriented","blockId":"test:oriented",)"
                                          R"("properties":[{"id":"test:block_state",)"
                                          R"("defaultValue":0}]}]})" ) ),
                "test-prototypes.json", blocks, prototypes, &sidecars );

            state = std::make_unique<WorldState>( chunks, idTable, sidecars, prototypes );
            state->setPersistenceSink( &sink );

            // Load the block-scope pilot block for the block-scope checks.
            if( loadPilotBlock )
                CHECK( state->setBlock( block, orientedId ) );
        }
    };
} // namespace

// ---------------------------------------------- mandatory per-scope contract
TEST_CASE( hierarchy_default_write_creates_no_entry )
{
    Fixture f;
    for( SidecarScope scope : SIX_SCOPES )
    {
        const WorldStateTarget target = targetFor( scope );
        const std::string id = scopeId( scope );
        // Writing the registered default (0) onto an untouched target is a
        // no-op: no entry is ever allocated.
        CHECK( !f.state->set( target, id, V( 0u ) ) );
        CHECK( !f.state->set( target, id, V( 0u ) ) );
    }
}

TEST_CASE( hierarchy_non_default_creates_one_sparse_entry )
{
    Fixture f;
    for( SidecarScope scope : SIX_SCOPES )
    {
        const WorldStateTarget target = targetFor( scope );
        const std::string id = scopeId( scope );

        CHECK( f.state->set( target, id, V( 42u ) ) );
        CHECK( f.state->has( target, id ) );
        CHECK( f.state->get( target, id ) == V( 42u ) );
        CHECK( !f.state->set( target, id, V( 42u ) ) ); // idempotent
    }
}

TEST_CASE( hierarchy_read_returns_the_exact_value )
{
    Fixture f;
    for( SidecarScope scope : SIX_SCOPES )
    {
        const WorldStateTarget target = targetFor( scope );
        const std::string id = scopeId( scope );
        CHECK( f.state->set( target, id, V( 7u ) ) );
        CHECK( f.state->set( target, id, V( 42u ) ) );
        CHECK( f.state->get( target, id ) == V( 42u ) ); // last write wins
    }
}

TEST_CASE( hierarchy_reset_to_default_removes_entry_and_address )
{
    Fixture f;
    for( SidecarScope scope : SIX_SCOPES )
    {
        const WorldStateTarget target = targetFor( scope );
        const std::string id = scopeId( scope );
        CHECK( f.state->set( target, id, V( 42u ) ) );
        CHECK( f.state->set( target, id, V( 0u ) ) ); // removal
        CHECK( !f.state->set( target, id, V( 0u ) ) ); // nothing left: no change
        CHECK( f.state->get( target, id ) == V( 0u ) ); // resolved default
    }
}

TEST_CASE( hierarchy_wrong_scope_is_rejected_every_way )
{
    Fixture f;
    for( SidecarScope a : SIX_SCOPES )
    {
        for( SidecarScope b : SIX_SCOPES )
        {
            if( a == b ) continue;
            const WorldStateTarget target = targetFor( b ); // scope b
            const std::string id = scopeId( a );            // property of scope a
            CHECK( !f.state->set( target, id, V( 1u ) ) );
            CHECK( !f.state->get( target, id ).has_value() );
            CHECK( !f.state->has( target, id ) );
        }
    }
    // Explicit spot checks for the contract examples: a block property on a
    // region target and a region property on a block target.
    CHECK( !f.state->has( REGION_A, scopeId( SidecarScope::Block ) ) );
    CHECK( !f.state->set( REGION_A, scopeId( SidecarScope::Block ), V( 1u ) ) );
    CHECK( !f.state->set( BlockAddress{}, scopeId( SidecarScope::Region ), V( 1u ) ) );
}

TEST_CASE( hierarchy_distinct_addresses_never_collide )
{
    Fixture f;
    const std::string id = scopeId( SidecarScope::Section );
    CHECK( f.state->set( WorldStateTarget( SECTION_A ), id, V( 1u ) ) );
    CHECK( f.state->set( WorldStateTarget( SECTION_B ), id, V( 2u ) ) );
    CHECK( f.state->set( WorldStateTarget( SECTION_C ), id, V( 3u ) ) );
    CHECK( f.state->get( WorldStateTarget( SECTION_A ), id ) == V( 1u ) );
    CHECK( f.state->get( WorldStateTarget( SECTION_B ), id ) == V( 2u ) );
    CHECK( f.state->get( WorldStateTarget( SECTION_C ), id ) == V( 3u ) );
    // Identity is canonical: SECTION_B shares region/section digits with A
    // but lives in another sector; C shares section digits but another
    // region - all three are distinct world-state targets.
    CHECK( WorldStateTarget( SECTION_A ) != WorldStateTarget( SECTION_B ) );
    CHECK( WorldStateTarget( SECTION_A ) != WorldStateTarget( SECTION_C ) );
    CHECK( WorldStateTarget( REGION_A ) != WorldStateTarget( REGION_B ) );
}

TEST_CASE( hierarchy_upper_scopes_never_materialize_containers )
{
    Fixture f( /* loadPilotBlock = */ false );
    // ChunkGroup, Section, Region and Sector writes must never create any
    // Chunk, ChunkGroup, Section, Region or Sector object.
    CHECK( f.state->set( GROUP_A, scopeId( SidecarScope::ChunkGroup ), V( 1u ) ) );
    CHECK( f.state->set( SECTION_A, scopeId( SidecarScope::Section ), V( 1u ) ) );
    CHECK( f.state->set( REGION_A, scopeId( SidecarScope::Region ), V( 1u ) ) );
    CHECK( f.state->set( SECTOR_A, scopeId( SidecarScope::Sector ), V( 1u ) ) );
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } );
    CHECK_EQ( f.chunks.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( hierarchy_persist_false_never_reaches_the_sink )
{
    Fixture f;
    const std::string id = "test:no_persist";
    CHECK( f.state->set( REGION_A, id, V( 1u ) ) );
    CHECK( f.state->get( REGION_A, id ) == V( 1u ) );
    CHECK_EQ( f.sink.propertyDeltaCount(), std::size_t{ 0 } );
    CHECK( f.state->set( REGION_A, id, V( 0u ) ) ); // removal also invisible
    CHECK_EQ( f.sink.propertyDeltaCount(), std::size_t{ 0 } );
}

TEST_CASE( hierarchy_removal_is_an_explicit_delta )
{
    Fixture f( /* loadPilotBlock = */ false ); // no block chunks in this world
    const std::string id = scopeId( SidecarScope::Region );
    CHECK( f.state->set( REGION_A, id, V( 42u ) ) );
    const auto &deltas = f.sink.propertyDeltas();
    const auto stored = deltas.find( std::make_pair( WorldStateTarget( REGION_A ), id ) );
    CHECK( stored != deltas.end() );
    if( stored != deltas.end() )
    {
        CHECK( stored->second.target.scope() == SidecarScope::Region );
        CHECK( stored->second.target == WorldStateTarget( REGION_A ) );
        CHECK( !stored->second.blockAddress().has_value() ); // no fake block
        CHECK( stored->second.value == V( 42u ) );
    }
    // Writing the default reports an explicit removal (nullopt), never a
    // magic default value.
    CHECK( f.state->set( REGION_A, id, V( 0u ) ) );
    const auto removed = deltas.find( std::make_pair( WorldStateTarget( REGION_A ), id ) );
    CHECK( removed != deltas.end() );
    if( removed != deltas.end() )
        CHECK( !removed->second.value.has_value() );
    // Hierarchy changes never mark a chunk dirty (only block-scope does).
    CHECK_EQ( f.sink.dirtyChunkCount(), std::size_t{ 0 } );
}

TEST_CASE( hierarchy_registered_default_is_the_removal_threshold )
{
    Fixture f;
    const std::string id = "test:region_default7"; // registered default 7
    CHECK( f.state->set( REGION_A, id, V( 5u ) ) );
    CHECK( f.state->get( REGION_A, id ) == V( 5u ) );
    CHECK( f.state->set( REGION_A, id, V( 7u ) ) ); // removal threshold is 7
    CHECK( f.state->get( REGION_A, id ) == V( 7u ) ); // resolves the default
    CHECK( f.state->get( REGION_B, id ) == V( 7u ) ); // untouched address
}

TEST_CASE( hierarchy_change_hook_delivers_target_and_property )
{
    Fixture f;
    std::vector<std::pair<WorldStateTarget, std::string>> seen;
    f.state->setOnTargetChange( [&]( const WorldStateTarget &target,
                                     const std::string &what ) {
        seen.emplace_back( target, what );
    } );
    CHECK( !f.state->set( REGION_A, scopeId( SidecarScope::Region ), V( 0u ) ) );
    CHECK( seen.empty() ); // no-ops never fire
    CHECK( f.state->set( SECTION_A, scopeId( SidecarScope::Section ), V( 5u ) ) );
    CHECK_EQ( seen.size(), std::size_t{ 1 } );
    if( !seen.empty() )
    {
        CHECK( seen[0].first == WorldStateTarget( SECTION_A ) );
        CHECK( seen[0].second == scopeId( SidecarScope::Section ) );
    }
}

TEST_CASE( hierarchy_enumeration_is_deterministic )
{
    HierarchySidecarStore store;
    // Scatter entries across scopes and addresses, including same-scope
    // ordering hazards.
    store.set( WorldStateTarget( SECTOR_A ), "test:sector_state", V( 1u ), V( 0u ) );
    store.set( WorldStateTarget( SECTOR_B ), "test:sector_state", V( 2u ), V( 0u ) );
    store.set( WorldStateTarget( SECTION_A ), "test:section_state", V( 3u ), V( 0u ) );
    store.set( WorldStateTarget( SECTION_C ), "test:section_state", V( 4u ), V( 0u ) );
    store.set( WorldStateTarget( SECTION_A ), "test:extra", V( 5u ), V( 0u ) );
    store.set( WorldStateTarget( CHUNK_A ), "test:chunk_state", V( 6u ), V( 0u ) );

    const auto first = store.enumerate();
    const auto again = store.enumerate();
    CHECK_EQ( first.size(), std::size_t{ 6 } );
    CHECK( first == again ); // identical on repetition
    CHECK( std::is_sorted( first.begin(), first.end(),
                           []( const auto &l, const auto &r ) {
                               return std::make_pair( l.target, l.propertyId ) <
                                      std::make_pair( r.target, r.propertyId );
                           } ) );
    CHECK( first.front().target == WorldStateTarget( CHUNK_A ) ); // chunk scope first
    CHECK( first.back().target == WorldStateTarget( SECTOR_B ) );
}

namespace
{
    template <typename Fn>
    bool throwsInvalidArgument( Fn &&fn )
    {
        try
        {
            fn();
        }
        catch( const std::invalid_argument & )
        {
            return true;
        }
        return false;
    }
} // namespace

TEST_CASE( hierarchy_target_rejects_non_canonical_addresses )
{
    // M01-B review (MAJOR): WorldStateTarget is the canonical-identity gate.
    // No non-canonical address may ever be stored; no normalization/silent
    // carry occurs - the address is rejected like the existing Chunk/
    // ChunkGroup contracts (std::invalid_argument).

    // Upper local digit at the radix: out of range.
    RegionAddress badRegion;
    badRegion.region.x = REGIONS_PER_SECTOR_EDGE;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( badRegion ); } ) );
    SectionAddress badSection;
    badSection.region.x = REGIONS_PER_SECTOR_EDGE - 1; // valid parent
    badSection.section.x = SECTIONS_PER_REGION_EDGE;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( badSection ); } ) );
    GroupAddress badGroup;
    badGroup.section.x = 0;
    badGroup.group.x = GROUPS_PER_SECTION_EDGE;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( badGroup ); } ) );
    ChunkAddress badChunk;
    badChunk.group.section.x = 0;
    badChunk.group.group.x = 0;
    badChunk.chunk.x = CHUNKS_PER_GROUP_EDGE;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( badChunk ); } ) );
    BlockAddress badBlock;
    badBlock.chunk.chunk.x = 0;
    badBlock.block.x = BLOCKS_PER_CHUNK_EDGE;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( badBlock ); } ) );

    // Negative local digits are out of range too.
    RegionAddress negativeRegion;
    negativeRegion.region.x = -1;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( negativeRegion ); } ) );
    GroupAddress negativeGroup;
    negativeGroup.group.y = -1;
    CHECK( throwsInvalidArgument( [&] { (void)WorldStateTarget( negativeGroup ); } ) );

    // The Variant constructor validates its contained address as well.
    CHECK( throwsInvalidArgument( [&] {
        (void)WorldStateTarget( WorldStateTarget::Variant( badRegion ) );
    } ) );

    // Legal boundary values stay legal (max valid local digits).
    RegionAddress maxRegion;
    maxRegion.region.x = REGIONS_PER_SECTOR_EDGE - 1;
    maxRegion.region.y = REGIONS_PER_SECTOR_EDGE - 1;
    CHECK( !throwsInvalidArgument( [&] { (void)WorldStateTarget( maxRegion ); } ) );
    SectionAddress maxSection;
    maxSection.region.x = REGIONS_PER_SECTOR_EDGE - 1;
    maxSection.section.x = SECTIONS_PER_REGION_EDGE - 1;
    CHECK( !throwsInvalidArgument( [&] { (void)WorldStateTarget( maxSection ); } ) );
    GroupAddress maxGroup;
    maxGroup.region.x = REGIONS_PER_SECTOR_EDGE - 1;
    maxGroup.section.x = SECTIONS_PER_REGION_EDGE - 1;
    maxGroup.group.x = GROUPS_PER_SECTION_EDGE - 1;
    CHECK( !throwsInvalidArgument( [&] { (void)WorldStateTarget( maxGroup ); } ) );
    ChunkAddress maxChunk;
    maxChunk.group = maxGroup;
    maxChunk.chunk.x = CHUNKS_PER_GROUP_EDGE - 1;
    BlockAddress maxBlock;
    maxBlock.chunk = maxChunk;
    maxBlock.block.x = BLOCKS_PER_CHUNK_EDGE - 1;
    CHECK( !throwsInvalidArgument( [&] { (void)WorldStateTarget( maxChunk ); } ) );
    CHECK( !throwsInvalidArgument( [&] { (void)WorldStateTarget( maxBlock ); } ) );

    // Sector remains the unbounded outermost digit: the full int64 range is
    // canonical.
    CHECK( !throwsInvalidArgument( [&] {
        (void)WorldStateTarget( SectorAddress{ SectorCoord{ std::numeric_limits<std::int64_t>::max(),
                                                             std::numeric_limits<std::int64_t>::min(),
                                                             -7 } } );
    } ) );
}

TEST_CASE( hierarchy_store_default_write_on_untouched_address_allocates_nothing )
{
    // M01-B review: a default write must never create a transient address
    // entry (SparseStore::set looks up before deciding removal).
    HierarchySidecarStore store;
    CHECK( !store.set( WorldStateTarget( CHUNK_A ), "test:chunk_state", V( 0u ), V( 0u ) ) );
    CHECK_EQ( store.addressCount( SidecarScope::Chunk ), std::size_t{ 0 } );
    CHECK_EQ( store.entryCount( SidecarScope::Chunk ), std::size_t{ 0 } );
    CHECK( !store.set( WorldStateTarget( REGION_A ), "test:region_state", V( 0u ), V( 0u ) ) );
    CHECK_EQ( store.addressCount( SidecarScope::Region ), std::size_t{ 0 } );
    CHECK_EQ( store.entryCount( SidecarScope::Region ), std::size_t{ 0 } );
    // The removal path still works on an existing entry.
    CHECK( store.set( WorldStateTarget( REGION_A ), "test:region_state", V( 5u ), V( 0u ) ) );
    CHECK_EQ( store.addressCount( SidecarScope::Region ), std::size_t{ 1 } );
    CHECK( store.set( WorldStateTarget( REGION_A ), "test:region_state", V( 0u ), V( 0u ) ) );
    CHECK_EQ( store.addressCount( SidecarScope::Region ), std::size_t{ 0 } );
    CHECK_EQ( store.entryCount( SidecarScope::Region ), std::size_t{ 0 } );
}

TEST_CASE( chunk_scope_write_never_materializes_a_chunk )
{
    // M01-01: the no-materialization guarantee starts at the chunk tier
    // itself, on a pristine ChunkManager.
    Fixture f( /* loadPilotBlock = */ false );
    CHECK( f.state->set( CHUNK_A, scopeId( SidecarScope::Chunk ), V( 1u ) ) );
    CHECK( f.state->get( CHUNK_A, scopeId( SidecarScope::Chunk ) ) == V( 1u ) );
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } );
    CHECK_EQ( f.chunks.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( block_property_change_fires_generic_and_legacy_hooks_once )
{
    Fixture f;
    std::vector<std::pair<WorldStateTarget, std::string>> generic;
    std::vector<std::pair<BlockAddress, std::string>> legacy;
    f.state->setOnTargetChange( [&]( const WorldStateTarget &target,
                                     const std::string &what ) {
        generic.emplace_back( target, what );
    } );
    f.state->setOnChange( [&]( const BlockAddress &address, const std::string &what ) {
        legacy.emplace_back( address, what );
    } );

    // 1. Block property change: generic target hook AND legacy block hook,
    //    each exactly once.
    CHECK( f.state->set( f.block, scopeId( SidecarScope::Block ), V( 1u ) ) );
    CHECK_EQ( generic.size(), std::size_t{ 1 } );
    CHECK_EQ( legacy.size(), std::size_t{ 1 } );
    if( !generic.empty() )
    {
        CHECK( generic[0].first == WorldStateTarget( f.block ) );
        CHECK( generic[0].second == scopeId( SidecarScope::Block ) );
    }
    if( !legacy.empty() )
        CHECK( legacy[0].first == f.block );

    // 2. Hierarchy property change: generic hook once, no legacy hook.
    CHECK( f.state->set( REGION_A, scopeId( SidecarScope::Region ), V( 1u ) ) );
    CHECK_EQ( generic.size(), std::size_t{ 2 } );
    CHECK_EQ( legacy.size(), std::size_t{ 1 } );

    // 3. No-op write: no hook at all.
    CHECK( !f.state->set( REGION_A, scopeId( SidecarScope::Region ), V( 1u ) ) );
    CHECK_EQ( generic.size(), std::size_t{ 2 } );
    CHECK_EQ( legacy.size(), std::size_t{ 1 } );

    // 4. Block replacement: generic target hook observable (what = "block").
    CHECK( f.state->setBlock( f.block, 0u ) ); // -> AIR
    CHECK_EQ( generic.size(), std::size_t{ 3 } );
    CHECK_EQ( legacy.size(), std::size_t{ 2 } );
    if( generic.size() >= 3u )
    {
        CHECK( generic[2].first == WorldStateTarget( f.block ) );
        CHECK( generic[2].second == "block" );
    }
}

TEST_CASE( hierarchy_store_per_scope_allocation_table )
{
    // M01-B review round 4 (MAJOR): the mandatory per-scope sparse
    // allocation/removal/address-isolation contract is proven DIRECTLY
    // against the store for every hierarchy scope, not only through
    // WorldState's resolved view - a zombie entry can never hide here.
    const std::array<SidecarScope, 5> SCOPES = {
        SidecarScope::Chunk, SidecarScope::ChunkGroup, SidecarScope::Section,
        SidecarScope::Region, SidecarScope::Sector
    };
    const std::array<WorldStateTarget, 5> TARGETS = {
        WorldStateTarget( CHUNK_A ),   WorldStateTarget( GROUP_A ),
        WorldStateTarget( SECTION_A ), WorldStateTarget( REGION_A ),
        WorldStateTarget( SECTOR_A )
    };
    const std::array<WorldStateTarget, 5> ALT = {
        WorldStateTarget( CHUNK_B ),   WorldStateTarget( GROUP_B ),
        WorldStateTarget( SECTION_B ), WorldStateTarget( REGION_B ),
        WorldStateTarget( SECTOR_B )
    };
    for( std::size_t i = 0; i < SCOPES.size(); ++i )
    {
        HierarchySidecarStore store;
        const SidecarScope scope = SCOPES[i];
        const WorldStateTarget target = TARGETS[i];
        const std::string id = scopeId( scope );

        // default -> nothing allocated anywhere.
        CHECK( !store.set( target, id, V( 0u ), V( 0u ) ) );
        CHECK_EQ( store.addressCount( scope ), std::size_t{ 0 } );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 0 } );

        // non-default -> exactly one sparse entry at exactly one address.
        CHECK( store.set( target, id, V( 42u ), V( 0u ) ) );
        CHECK_EQ( store.addressCount( scope ), std::size_t{ 1 } );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 1 } );
        CHECK( store.get( target, id ) == V( 42u ) );

        // same value again -> no state change, no extra entry.
        CHECK( !store.set( target, id, V( 42u ), V( 0u ) ) );
        CHECK_EQ( store.addressCount( scope ), std::size_t{ 1 } );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 1 } );

        // reset -> entry and address gone.
        CHECK( store.set( target, id, V( 0u ), V( 0u ) ) );
        CHECK_EQ( store.addressCount( scope ), std::size_t{ 0 } );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 0 } );

        // a second, distinct address of the same scope stays isolated.
        CHECK( store.set( target, id, V( 1u ), V( 0u ) ) );
        CHECK( store.set( ALT[i], id, V( 2u ), V( 0u ) ) );
        CHECK_EQ( store.addressCount( scope ), std::size_t{ 2 } );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 2 } );
        CHECK( store.get( target, id ) == V( 1u ) );
        CHECK( store.get( ALT[i], id ) == V( 2u ) );
        CHECK_EQ( store.entryCount( scope ), std::size_t{ 2 } );
    }
}

TEST_CASE( hierarchy_block_scope_allocation_via_chunk_sidecar )
{
    // Block scope keeps its canonical store inside the Chunk; the same
    // table contract is proven through the chunk sidecar directly.
    Fixture f;
    const BlockAddress &b = f.block;
    const std::string id = scopeId( SidecarScope::Block );

    // default write -> no sidecar exists at all.
    CHECK( !f.state->set( b, id, V( 0u ) ) );
    CHECK( f.chunks.blockPropertySidecarInChunk( b.chunk, id ) == nullptr );
    // non-default -> exactly one entry in the chunk sidecar.
    CHECK( f.state->set( b, id, V( 42u ) ) );
    const auto *sidecar = f.chunks.blockPropertySidecarInChunk( b.chunk, id );
    CHECK( sidecar != nullptr );
    if( sidecar )
        CHECK_EQ( sidecar->entryCount(), std::size_t{ 1 } );
    // reset -> sidecar dropped again.
    CHECK( f.state->set( b, id, V( 0u ) ) );
    CHECK( f.chunks.blockPropertySidecarInChunk( b.chunk, id ) == nullptr );
}

TEST_CASE( hierarchy_float_properties_are_finite_by_contract )
{
    // Round 6 (MAJOR): non-finite floats would break the equality-based
    // sparse semantics (NaN != NaN => every rewrite looks like a change and
    // the default-threshold removal can never fire). The WorldState path
    // therefore rejects NaN/Infinity exactly like the registry validation.
    Fixture f( /* loadPilotBlock = */ false );
    const WorldStateTarget target( REGION_A );
    const std::string id = "test:region_float";

    const auto VF = []( float v ) { return PropertyValue{ v }; };

    // NaN / +Inf / -Inf are rejected through the real WorldState path.
    CHECK( !f.state->set( target, id, VF( std::numeric_limits<float>::quiet_NaN() ) ) );
    CHECK( !f.state->set( target, id, VF( std::numeric_limits<float>::infinity() ) ) );
    CHECK( !f.state->set( target, id, VF( -std::numeric_limits<float>::infinity() ) ) );
    CHECK_EQ( f.sink.propertyDeltaCount(), std::size_t{ 0 } );

    // Finite values keep the stable no-op and removal semantics.
    CHECK( f.state->set( target, id, VF( 3.5f ) ) );
    CHECK( f.state->get( target, id ) == VF( 3.5f ) );
    CHECK( !f.state->set( target, id, VF( 3.5f ) ) ); // same value: no change
    CHECK( f.state->set( target, id, VF( 0.0f ) ) );  // removal against default
    CHECK( f.state->get( target, id ) == VF( 0.0f ) ); // resolved default
}

int main() { return test::runAll(); }
