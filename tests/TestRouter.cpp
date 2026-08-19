#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRouter.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace world;
    using namespace world::communication;

    bool rejected( const std::function<void()> &fn )
    {
        try
        {
            fn();
        }
        catch( const CommunicationError & )
        {
            return true;
        }
        return false;
    }

    struct Fixture
    {
        BlockRegistry blocks;
        BlockIdTable idTable;
        SidecarRegistry sidecars;
        PrototypeRegistry prototypes;
        ChunkManager chunks;
        std::unique_ptr<WorldState> state;
        MemoryPersistenceSink sink;
        MessageIdSource ids;
        CommunicationRouter router;

        std::uint16_t stoneId = 0;

        Fixture()
        {
            BlockDef air;
            air.id = "core:air";
            air.displayName = "Air";
            blocks.insert( air );
            BlockDef stone;
            stone.id = "core:stone";
            stone.displayName = "Stone";
            blocks.insert( stone );
            idTable = BlockIdTable( blocks );
            stoneId = idTable.indexOf( "core:stone" );

            state = std::make_unique<WorldState>( chunks, idTable, sidecars, prototypes );
            state->setPersistenceSink( &sink );
            registerBlockCommandHandlers( router, *state, ids );
        }

        CommunicationEnvelope command( const std::string &action, Payload payload,
                                       const WorldStateTarget &target )
        {
            CommunicationEnvelope env;
            env.messageId = ids.next();
            env.kind = EnvelopeKind::Command;
            env.sender = "player:1";
            env.receiver = "world:state";
            env.context = "core:world";
            env.action = action;
            env.target = target;
            env.payload = std::move( payload );
            return env;
        }
    };
} // namespace

TEST_CASE( router_dispatch_place_block_command )
{
    Fixture f;

    const BlockAddress placement = fromOriginOffset( 2, 3, 2 ); // adjacent above

    // The envelope target (the placement cell) is authoritative. The chunk is
    // loaded first - placement into unloaded chunks is rejected (round 5).
    f.chunks.loadChunk( placement.chunk );
    const CommunicationEnvelope env = f.command(
        ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
        WorldStateTarget( placement ) );
    const auto result = f.router.dispatch( env );

    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    CHECK_EQ( f.chunks.blockAt( placement ), f.stoneId ); // visible change
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
    {
        CHECK( reply->ok );
        CHECK( reply->error.empty() );
    }
    CHECK( result.replies[0].replyTo == env.receiver ); // logical return address
    CHECK( result.replies[0].sender == env.receiver );
    CHECK( result.replies[0].receiver == env.sender );
    validateEnvelope( result.replies[0] );

    // The dirty/persistence abstraction saw the mutation through WorldState.
    CHECK( f.sink.isDirty( placement.chunk ) );
    CHECK_EQ( f.sink.blockDeltas().at( placement ).newRuntimeId, f.stoneId );
}

TEST_CASE( router_remove_block_command )
{
    Fixture f;
    const BlockAddress block = fromOriginOffset( 4, 4, 4 );
    CHECK( f.state->setBlock( block, f.stoneId ) );

    const CommunicationEnvelope env =
        f.command( ACTION_BLOCK_REMOVE, std::monostate{}, WorldStateTarget( block ) );
    const auto result = f.router.dispatch( env );

    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    const auto after = f.state->blockAt( block );
    CHECK( after.has_value() );
    if( after )
        CHECK_EQ( *after, 0u );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( reply->ok );
}

TEST_CASE( router_rejects_invalid_commands_without_mutation )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 1, 1, 1 );

    // Unknown action: loud CommunicationError.
    auto unknown = f.command( "core:block.spawn", std::monostate{}, WorldStateTarget( target ) );
    CHECK( rejected( [&] { (void)f.router.dispatch( unknown ); } ) );

    // Wrong payload type for the handler.
    auto wrongPayload = f.command( ACTION_BLOCK_PLACE, std::monostate{},
                                   WorldStateTarget( target ) );
    const auto rej = f.router.dispatch( wrongPayload );
    CHECK_EQ( rej.replies.size(), std::size_t{ 1 } );
    const auto *reply = std::get_if<CommandResultPayload>( &rej.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
    {
        CHECK( !reply->ok );
        CHECK( reply->error.find( "payload" ) != std::string::npos );
    }

    // Non-block target (scope rejection): a region cannot be placed into.
    const RegionAddress regionA{ { 1, 0, 0 }, { 3, 0, 0 } };
    auto regionTarget = f.command( ACTION_BLOCK_PLACE,
                                   BlockPlacePayload{ f.stoneId },
                                   WorldStateTarget( regionA ) );
    const auto rejScope = f.router.dispatch( regionTarget );
    CHECK_EQ( rejScope.replies.size(), std::size_t{ 1 } );
    const auto *scopeReply = std::get_if<CommandResultPayload>( &rejScope.replies[0].payload );
    CHECK( scopeReply != nullptr );
    if( scopeReply )
        CHECK( !scopeReply->ok );

    // No chunk was materialized by the rejected commands.
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } );
    CHECK_EQ( f.chunks.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( router_rejects_wrong_envelope_kind_for_command_actions )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 3, 3, 3 );

    // An Event with the same action must NEVER reach the Command mutation:
    // the router matches (action, kind) exactly (M02 review).
    auto event = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                            WorldStateTarget( target ) );
    event.kind = EnvelopeKind::Event;
    CHECK( rejected( [&] { (void)f.router.dispatch( event ); } ) );
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } ); // nothing mutated

    auto query = f.command( ACTION_BLOCK_REMOVE, std::monostate{},
                            WorldStateTarget( target ) );
    query.kind = EnvelopeKind::Query;
    CHECK( rejected( [&] { (void)f.router.dispatch( query ); } ) );
}

TEST_CASE( router_duplicate_handler_registration_is_loud )
{
    Fixture f;
    CHECK( rejected( [&] {
        // The fixture already registered exactly this bus route.
        f.router.registerHandler( ACTION_BLOCK_PLACE, EnvelopeKind::Command,
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {},
                                  std::optional<std::string>{ "world:state" },
                                  std::optional<std::string>{ "core:world" } );
    } ) );
}

TEST_CASE( router_target_is_authoritative_over_payload )
{
    Fixture f;
    const BlockAddress placement = fromOriginOffset( 5, 5, 5 );
    f.chunks.loadChunk( placement.chunk ); // loaded cell, round-5 gate
    // The handler mutates ONLY the canonical target; a crafted payload with
    // extra/missing addresses can never redirect the mutation.
    const auto env = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                WorldStateTarget( placement ) );
    const auto result = f.router.dispatch( env );
    CHECK( result.handled );
    CHECK_EQ( f.chunks.blockAt( placement ), f.stoneId ); // exactly the target
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 1 } );  // only one chunk existed
}

TEST_CASE( router_remove_rejects_non_empty_payload )
{
    Fixture f;
    const BlockAddress block = fromOriginOffset( 6, 6, 6 );
    f.chunks.setBlock( block, f.stoneId );

    // round-3 fix: a wrong typed payload on remove must NOT mutate.
    const auto env = f.command( ACTION_BLOCK_REMOVE, BlockPlacePayload{ f.stoneId },
                                WorldStateTarget( block ) );
    const auto result = f.router.dispatch( env );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( !reply->ok );
    CHECK_EQ( f.chunks.blockAt( block ), f.stoneId ); // untouched
}

TEST_CASE( router_place_air_runtime_is_rejected )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 9, 9, 9 );
    // round-3 fix: placement of AIR (runtime 0) would be a hidden second
    // removal path; it is rejected.
    const auto env = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ 0u },
                                WorldStateTarget( target ) );
    const auto result = f.router.dispatch( env );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( !reply->ok );
    CHECK_EQ( f.chunks.chunkCount(), std::size_t{ 0 } ); // nothing materialized
}

TEST_CASE( router_place_onto_occupied_cell_is_rejected )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 2, 8, 2 );
    f.chunks.setBlock( target, f.stoneId );
    // round-4 fix: placement must never silently overwrite a non-AIR block.
    const auto env = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                WorldStateTarget( target ) );
    const auto result = f.router.dispatch( env );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
    {
        CHECK( !reply->ok );
        CHECK( reply->error.find( "occupied" ) != std::string::npos );
    }
    CHECK_EQ( f.chunks.blockAt( target ), f.stoneId ); // unmodified cell
}

TEST_CASE( router_place_into_unloaded_position_is_rejected_without_materialization )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 7, 7, 7 );
    // Round 5: writing into an unloaded chunk would create an all-AIR chunk
    // ahead of deterministic worldgen - placement only happens in loaded
    // cells, and no chunk may be materialized by the rejected intent.
    const auto env = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                WorldStateTarget( target ) );
    const auto result = f.router.dispatch( env );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
    {
        CHECK( !reply->ok );
        CHECK( reply->error.find( "not loaded" ) != std::string::npos );
    }
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } ); // later WorldGen untouched
    CHECK_EQ( f.chunks.chunkCount(), std::size_t{ 0 } );
}

TEST_CASE( router_remove_into_unloaded_position_is_rejected_without_materialization )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 8, 8, 8 );
    const auto env = f.command( ACTION_BLOCK_REMOVE, std::monostate{},
                                WorldStateTarget( target ) );
    const auto result = f.router.dispatch( env );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( !reply->ok );
    CHECK_EQ( f.chunks.groupCount(), std::size_t{ 0 } );
}

TEST_CASE( router_routes_by_receiver_dimension )
{
    Fixture f;
    // Two handlers for the same action+kind, differentiated by receiver.
    // M03 Round 1 (MAJOR 4): a handler output must itself be a valid
    // envelope - the old synthetic `{}` reply is a contract violation now.
    f.router.registerHandler( "test:routed", EnvelopeKind::Command,
                              [&]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( makeReply( cause, CommandResultPayload{ true, {}, std::nullopt },
                                      f.ids ) );
    }, std::string{ "machine:a" } );
    f.router.registerHandler( "test:routed", EnvelopeKind::Command,
                              [&]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( makeReply( cause, CommandResultPayload{ true, {}, std::nullopt },
                                      f.ids ) );
    }, std::string{ "machine:b" } );

    auto env = f.command( "test:routed", std::monostate{}, WorldStateTarget( BlockAddress{} ) );
    env.receiver = "machine:a";
    CHECK_EQ( f.router.dispatch( env ).replies.size(), std::size_t{ 1 } );
    env.receiver = "machine:b";
    CHECK_EQ( f.router.dispatch( env ).replies.size(), std::size_t{ 1 } );
    env.receiver = "machine:c"; // no exact route: falls back to wildcard -> none
    CHECK( rejected( [&] { (void)f.router.dispatch( env ); } ) );
}

TEST_CASE( router_routes_by_context_dimension )
{
    Fixture f;
    f.router.registerHandler( "test:contextual", EnvelopeKind::Command,
                              [&]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( makeReply( cause, CommandResultPayload{ true, {}, std::nullopt },
                                      f.ids ) );
    }, std::nullopt, std::string{ "core:world" } );
    auto env = f.command( "test:contextual", std::monostate{},
                          WorldStateTarget( BlockAddress{} ) );
    env.context = "core:world";
    CHECK_EQ( f.router.dispatch( env ).replies.size(), std::size_t{ 1 } );
    env.context = "mod:other";
    CHECK( rejected( [&] { (void)f.router.dispatch( env ); } ) );
}

TEST_CASE( router_validation_rejects_bad_registrations )
{
    Fixture f;
    CHECK( rejected( [&] {
        f.router.registerHandler( "notnamespaced", EnvelopeKind::Command,
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {} );
    } ) );
    CHECK( rejected( [&] {
        // An out-of-range kind forged through a runtime-computed byte value
        // (the analyzer cannot fold this into a constant path).
        const std::uint8_t fabricated =
            static_cast<std::uint8_t>( 200u + f.ids.next() % 8u );
        f.router.registerHandler( "test:badkind",
                                  static_cast<EnvelopeKind>( fabricated ), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {} );
    } ) );
    // Duplicate exact route is loud.
    CHECK( rejected( [&] {
        f.router.registerHandler( "core:block.place", EnvelopeKind::Command,
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {},
                                  std::optional<std::string>{ "world:state" },
                                  std::optional<std::string>{ "core:world" } );
    } ) );
    // Empty handler registration is loud.
    CHECK( rejected( [&] {
        f.router.registerHandler( "test:empty", EnvelopeKind::Command, {} );
    } ) );
    // Non-namespaced context is loud.
    CHECK( rejected( [&] {
        f.router.registerHandler( "test:ctx", EnvelopeKind::Command,
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {},
                                  std::nullopt, std::string{ "notnamespaced" } );
    } ) );
}

TEST_CASE( router_block_commands_require_the_bus_address )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 4, 4, 4 );
    f.chunks.loadChunk( target.chunk );

    // Correctly addressed world:state + core:world -> handler runs.
    auto good = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                           WorldStateTarget( target ) );
    good.receiver = "world:state";
    good.context = "core:world";
    const auto ok = f.router.dispatch( good );
    CHECK( ok.handled );
    CHECK_EQ( f.chunks.blockAt( target ), f.stoneId );

    // Wrong receiver + correct action -> NO mutation (no wildcard route for
    // the authoritative block commands).
    CHECK( f.state->setBlock( target, 0u ) );
    auto wrongReceiver = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                    WorldStateTarget( target ) );
    wrongReceiver.receiver = "inventory:system";
    wrongReceiver.context = "core:world";
    CHECK( rejected( [&] { (void)f.router.dispatch( wrongReceiver ); } ) );
    CHECK_EQ( f.chunks.blockAt( target ), 0u );

    // Wrong context + correct action -> NO mutation.
    auto wrongContext = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                   WorldStateTarget( target ) );
    wrongContext.receiver = "world:state";
    wrongContext.context = "inventory:context";
    CHECK( rejected( [&] { (void)f.router.dispatch( wrongContext ); } ) );
    CHECK_EQ( f.chunks.blockAt( target ), 0u );

    // Wrong receiver AND context -> NO mutation.
    auto wrongBoth = f.command( ACTION_BLOCK_PLACE, BlockPlacePayload{ f.stoneId },
                                WorldStateTarget( target ) );
    wrongBoth.receiver = "inventory:system";
    wrongBoth.context = "inventory:context";
    CHECK( rejected( [&] { (void)f.router.dispatch( wrongBoth ); } ) );
    CHECK_EQ( f.chunks.blockAt( target ), 0u );
}

TEST_CASE( router_match_priority_is_provable )
{
    Fixture f;
    // Four routes for one action/kind; each reports which route ran.
    std::vector<std::string> chosen;
    const auto record = [&chosen]( std::string tag ) {
        return [&chosen, tag = std::move( tag )]( const CommunicationEnvelope &,
                                                  std::vector<CommunicationEnvelope> & ) {
            chosen.push_back( tag );
        };
    };
    f.router.registerHandler( "test:prio", EnvelopeKind::Command, record( "wildcard" ) );
    f.router.registerHandler( "test:prio", EnvelopeKind::Command, record( "receiver" ),
                              std::string{ "machine:x" } );
    f.router.registerHandler( "test:prio", EnvelopeKind::Command, record( "context" ),
                              std::nullopt, std::string{ "core:world" } );
    f.router.registerHandler( "test:prio", EnvelopeKind::Command, record( "exact" ),
                              std::string{ "machine:x" }, std::string{ "core:world" } );

    auto env = f.command( "test:prio", std::monostate{}, WorldStateTarget( BlockAddress{} ) );

    env.receiver = "machine:x";
    env.context = "core:world";
    (void)f.router.dispatch( env );
    CHECK_EQ( chosen.back(), "exact" ); // exact beats receiver/context/wildcard

    env.context = "mod:other";
    (void)f.router.dispatch( env );
    CHECK_EQ( chosen.back(), "receiver" ); // receiver-only beats context/wildcard

    env.receiver = "other:machine";
    env.context = "core:world";
    (void)f.router.dispatch( env );
    CHECK_EQ( chosen.back(), "context" ); // context-only beats wildcard

    env.receiver = "other:machine";
    env.context = "mod:other";
    (void)f.router.dispatch( env );
    CHECK_EQ( chosen.back(), "wildcard" ); // last resort
}

TEST_CASE( router_empty_context_string_is_wildcard )
{
    Fixture f;
    // Round 7: an explicitly EMPTY context string equals the wildcard
    // (contract: nullopt OR "" = any context).
    bool ran = false;
    f.router.registerHandler( "test:emptynone", EnvelopeKind::Command,
                              [&ran]( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) { ran = true; },
                              std::string{ "machine:w" }, std::string{ "" } );
    auto env = f.command( "test:emptynone", std::monostate{},
                          WorldStateTarget( BlockAddress{} ) );
    env.receiver = "machine:w";
    env.context = "core:anything";
    (void)f.router.dispatch( env );
    CHECK( ran ); // empty-string registration acted as wildcard
    // A truly specific non-namespaced context stays CommunicationError.
    CHECK( rejected( [&] {
        f.router.registerHandler( "test:ctx", EnvelopeKind::Command,
                                  []( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) {},
                                  std::nullopt, std::string{ "notnamespaced" } );
    } ) );
}

int main() { return test::runAll(); }