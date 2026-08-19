#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/BoundedEnvelopeQueue.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRegistries.h"
#include "world/communication/CommunicationRouter.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"

#include <functional>
#include <string>
#include <type_traits>
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

    CommunicationEnvelope baseEnvelope( std::uint64_t id, EnvelopeKind kind,
                                        const std::string &action, const std::string &receiver,
                                        const std::string &context )
    {
        CommunicationEnvelope env;
        env.messageId = id;
        env.kind = kind;
        env.sender = "player:1";
        env.receiver = receiver;
        env.context = context;
        env.action = action;
        return env;
    }

    struct Fixture
    {
        MessageIdSource ids;
        CommunicationRouter router;
    };
} // namespace

// =====================================================================
// MAJOR 1: message ids are unique within one communication runtime
// =====================================================================

static_assert( !std::is_copy_constructible_v<MessageIdSource> );
static_assert( !std::is_copy_assignable_v<MessageIdSource> );
static_assert( !std::is_move_constructible_v<MessageIdSource> );

TEST_CASE( m03_message_ids_are_unique_within_one_runtime_context )
{
    // The runtime owns the single id source of its context: two producers
    // (input controller today, timer/Lua producers in later rounds) draw from
    // the SAME source, so ids can never collide. A copied/forked source - the
    // old silent collision path - is impossible by construction (non-copyable).
    CommunicationRuntime bus{ 8, 8 };
    const auto producerA = [&bus]() { return bus.nextMessageId(); };
    const auto producerB = [&bus]() { return bus.nextMessageId(); };
    const std::uint64_t a1 = producerA();
    const std::uint64_t b1 = producerB();
    const std::uint64_t a2 = producerA();
    CHECK_EQ( a1, std::uint64_t{ 1 } );
    CHECK_EQ( b1, std::uint64_t{ 2 } );
    CHECK_EQ( a2, std::uint64_t{ 3 } );
    CHECK( a1 != b1 );
    CHECK( b1 != a2 );
}

// =====================================================================
// MAJOR 2: Signal / Slot / Action are real semantic registries
// =====================================================================

TEST_CASE( m03_registries_are_real_semantic_structures )
{
    // The explicit three-registry path: declare the signal (the semantic
    // event/request + its payload contract), register the action (the
    // executable reaction), bind the slot (this receiver reacts to the
    // signal, expects the schema, executes the action). Routing selects the
    // slot; binding resolves the action - they are separate structures, not
    // comments over one router map.
    CommunicationRuntime bus{ 8, 8 };
    bus.declareSignal( "test:ring", EnvelopeKind::Query, PayloadSchema::Query );
    bus.registerAction( "test:answer", [&bus]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, PropertyValue{ 42u } } ) );
    } );
    bus.registerSlot( "test:ring", EnvelopeKind::Query, std::string{ "world:state" },
                      std::string{ "core:world" }, std::nullopt, PayloadSchema::Query,
                      "test:answer" );

    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 1 } );

    auto query = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query, "test:ring",
                               "world:state", "core:world" );
    query.payload = QueryPayload{ "count" };
    const auto result = bus.dispatch( query );
    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    const auto *answer = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( answer != nullptr );
    if( answer && answer->value )
        CHECK_EQ( std::get<std::uint32_t>( *answer->value ), std::uint32_t{ 42 } );

    // Wiring mistakes fail at registration:
    //  - a slot for an undeclared signal;
    CHECK( rejected( [&] {
        bus.registerSlot( "test:ghost", EnvelopeKind::Query, std::nullopt, std::nullopt,
                          std::nullopt, PayloadSchema::Query, "test:answer" );
    } ) );
    //  - a slot bound to an unregistered action;
    CHECK( rejected( [&] {
        bus.registerSlot( "test:ring", EnvelopeKind::Query, std::nullopt, std::nullopt,
                          std::nullopt, PayloadSchema::Query, "test:missing" );
    } ) );
    //  - a duplicate exact slot.
    CHECK( rejected( [&] {
        bus.registerSlot( "test:ring", EnvelopeKind::Query, std::string{ "world:state" },
                          std::string{ "core:world" }, std::nullopt, PayloadSchema::Query,
                          "test:answer" );
    } ) );
    // All three failures left the registries untouched.
    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 1 } );
}

// =====================================================================
// MAJOR 3 + findings 1/2: queues, exact dispatch, one delivery route
// =====================================================================

TEST_CASE( m03_dispatch_executes_exactly_the_passed_envelope )
{
    // dispatch(B) must execute B synchronously - never an older A that still
    // sits in the inbound queue; the result belongs to B and A stays pending.
    CommunicationRuntime bus{ 4, 4 };
    std::vector<std::uint64_t> executed;
    bus.registerHandler( "test:q", EnvelopeKind::Command,
                         [&executed]( const CommunicationEnvelope &env,
                                      std::vector<CommunicationEnvelope> & ) {
        executed.push_back( env.messageId );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const auto a = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                 "world:state", "core:world" );
    const auto b = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                 "world:state", "core:world" );
    CHECK( bus.submit( a ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );
    CHECK( executed.empty() ); // nothing ran before dispatch

    const auto bResult = bus.dispatch( b );
    CHECK( bResult.handled );
    CHECK_EQ( executed.size(), std::size_t{ 1 } );
    if( !executed.empty() )
        CHECK_EQ( executed[0], b.messageId ); // B ran, not A
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } ); // A is still pending
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 0 } );

    const auto aResult = bus.pumpOne();
    CHECK( aResult.handled );
    CHECK_EQ( executed.size(), std::size_t{ 2 } );
    if( executed.size() >= 2 )
        CHECK_EQ( executed[1], a.messageId ); // later pump processes A
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );

    // FIFO of the async path is preserved: submit order == execution order.
    const auto c = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                 "world:state", "core:world" );
    CHECK( bus.submit( c ) );
    (void)bus.pumpOne();
    CHECK_EQ( executed.size(), std::size_t{ 3 } );
    if( executed.size() >= 3 )
        CHECK_EQ( executed[2], c.messageId );
}

TEST_CASE( m03_one_delivery_route_per_message )
{
    // A message and its outputs are delivered on EXACTLY one route:
    //  - sync dispatch(): outputs in the returned DispatchResult, B untouched;
    //  - async submit()/pump*(): outputs ONLY in queue B (nextOutput()).
    CommunicationRuntime bus{ 8, 1 }; // tiny outbound: sync path ignores it
    bus.registerHandler( "test:sync", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    auto env1 = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:sync",
                              "world:state", "core:world" );
    const auto sync = bus.dispatch( env1 );
    CHECK_EQ( sync.replies.size(), std::size_t{ 1 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 0 } ); // B stays EMPTY on sync

    auto env2 = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:sync",
                              "world:state", "core:world" );
    CHECK( bus.submit( env2 ) );
    const auto pump = bus.pumpOne();
    CHECK( pump.handled );
    CHECK_EQ( pump.replies.size(), std::size_t{ 0 } ); // queue path returns no outputs
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // ... they are exclusively in B
    const auto out = bus.nextOutput();
    CHECK( out.has_value() );
    if( out )
    {
        CHECK( out->kind == EnvelopeKind::Reply );
        CHECK( out->correlationId.has_value() );
        if( out->correlationId )
            CHECK_EQ( *out->correlationId, env2.messageId );
    }
    CHECK( !bus.nextOutput().has_value() ); // exactly once, not multi-consumable
}

TEST_CASE( m03_bounded_queues_are_the_production_queue_path )
{
    // submit() -> inbound A; pumpOne()/pumpAll() execute FIFO; no outputs are
    // fabricated on this path (the handler here produces none).
    CommunicationRuntime bus{ 2, 2 };
    std::vector<std::uint64_t> executed;
    bus.registerHandler( "test:q", EnvelopeKind::Command,
                         [&executed]( const CommunicationEnvelope &env,
                                      std::vector<CommunicationEnvelope> & ) {
        executed.push_back( env.messageId );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const auto a = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                 "world:state", "core:world" );
    const auto b = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                 "world:state", "core:world" );
    const auto full = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:q",
                                    "world:state", "core:world" );
    CHECK( bus.submit( a ) );
    CHECK( bus.submit( b ) );
    CHECK( !bus.submit( full ) ); // loud backpressure: full, nothing stored
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 2 } );

    CHECK( bus.pumpOne().handled );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );
    if( !executed.empty() )
        CHECK_EQ( executed[0], a.messageId ); // FIFO: oldest first

    bus.pumpAll();
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    if( executed.size() >= 2 )
        CHECK_EQ( executed[1], b.messageId );
}

// =====================================================================
// FINDING 3: outbound delivery is staged and committed atomically
// =====================================================================

TEST_CASE( m03_outbound_backpressure_happens_before_handler_execution )
{
    // MAJOR 1+3: pumpOne() pre-flights the outbound capacity BEFORE the
    // handler runs. On backpressure: defined CommunicationError, NO side
    // effect, the envelope STAYS in A, nothing is delivered.
    CommunicationRuntime bus{ 8, 1 };
    int sideEffects = 0;
    // A separate filler route fills the outbound queue without touching the
    // side-effect counter of the route under test.
    bus.registerHandler( "test:fill", EnvelopeKind::Query,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::nullopt, std::nullopt, std::nullopt, PayloadSchema::Query );
    bus.registerHandler( "test:mutate", EnvelopeKind::Command,
                         [&bus, &sideEffects]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        ++sideEffects;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    // Default Command contract: maxOutputs = 1 (one correlated Reply).

    // Fill B completely with one already-processed reply (capacity 1).
    auto filler = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query,
                                "test:fill", "world:state", "core:world" );
    filler.payload = QueryPayload{ "x" };
    CHECK( bus.submit( filler ) );
    (void)bus.pumpOne();
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // B is now full

    // Submit the mutating command; pumpOne must fail BEFORE any side effect.
    const auto command = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                       "test:mutate", "world:state", "core:world" );
    CHECK( bus.submit( command ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );
    std::string errorText;
    bool threw = false;
    try
    {
        (void)bus.pumpOne();
    }
    catch( const CommunicationError &error )
    {
        threw = true;
        errorText = error.what();
    }
    CHECK( threw );
    CHECK( errorText.find( "backpressure" ) != std::string::npos ); // defined error
    CHECK_EQ( sideEffects, 0 );                // handler did NOT run
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } ); // envelope stays pending
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // B unchanged

    // Drain B, then pump again: the handler runs EXACTLY once, the input is
    // consumed and the reply appears EXACTLY once in B.
    CHECK( bus.nextOutput().has_value() );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 0 } );
    const auto result = bus.pumpOne();
    CHECK( result.handled );
    CHECK_EQ( sideEffects, 1 );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );
    const auto reply = bus.nextOutput();
    CHECK( reply.has_value() );
    if( reply )
    {
        CHECK( reply->kind == EnvelopeKind::Reply );
        if( reply->correlationId )
            CHECK_EQ( *reply->correlationId, command.messageId );
    }
    CHECK( !bus.nextOutput().has_value() );

    // Permanent undeliverability (MAJOR 2): with outbound capacity 0 the
    // async path can NEVER deliver a reply-producing command - submit()
    // rejects it BEFORE it enters A (no poison at the queue head, no side
    // effect). The sync dispatch stays fully independent of B.
    CommunicationRuntime zero{ 8, 0 };
    int zeroEffects = 0;
    zero.registerHandler( "test:zero", EnvelopeKind::Command,
                          [&zero, &zeroEffects]( const CommunicationEnvelope &cause,
                                                 std::vector<CommunicationEnvelope> &replies ) {
        ++zeroEffects;
        replies.push_back( zero.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::nullopt, std::nullopt, std::nullopt, PayloadSchema::None );
    auto zeroEnv = baseEnvelope( zero.nextMessageId(), EnvelopeKind::Command,
                                 "test:zero", "world:state", "core:world" );
    const auto zeroSync = zero.dispatch( zeroEnv ); // sync: independent of B
    CHECK( zeroSync.handled );
    CHECK_EQ( zeroSync.replies.size(), std::size_t{ 1 } );
    auto zeroAsync = baseEnvelope( zero.nextMessageId(), EnvelopeKind::Command,
                                   "test:zero", "world:state", "core:world" );
    CHECK( rejected( [&] { (void)zero.submit( zeroAsync ); } ) );
    CHECK_EQ( zeroEffects, 1 ); // only the sync dispatch ran the handler
    CHECK_EQ( zero.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( zero.pendingOutbound(), std::size_t{ 0 } );

    // The sync dispatch convenience is NOT affected by the outbound capacity
    // (its delivery route is the returned result, not the queue).
    CommunicationRuntime narrowSync{ 2, 0 };
    narrowSync.registerHandler( "test:quiet", EnvelopeKind::Query,
                                [&narrowSync]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( narrowSync.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::nullopt, std::nullopt, std::nullopt, PayloadSchema::None );
    auto syncQuery = baseEnvelope( narrowSync.nextMessageId(), EnvelopeKind::Query,
                                   "test:quiet", "world:state", "core:world" );
    const auto syncResult = narrowSync.dispatch( syncQuery );
    CHECK_EQ( syncResult.replies.size(), std::size_t{ 1 } ); // delivered on the result route
    CHECK_EQ( narrowSync.pendingOutbound(), std::size_t{ 0 } );
}

TEST_CASE( m03_permanently_undeliverable_messages_are_not_queued )
{
    // MAJOR 2: a message whose OutputContract exceeds the OUTBOUND CAPACITY
    // can never be processed on the async path. submit() must reject it
    // BEFORE enqueueing - otherwise it would block the queue head forever.
    CommunicationRuntime bus{ 4, 0 };
    bool commandRan = false;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&bus, &commandRan]( const CommunicationEnvelope &cause,
                                              std::vector<CommunicationEnvelope> &replies ) {
        commandRan = true;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    // Default Command contract: maxOutputs = 1 > outbound capacity 0.

    auto asyncCmd = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:mut",
                                  "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.submit( asyncCmd ); } ) ); // permanent: not queueable
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );          // never entered A
    CHECK( !commandRan );

    // Head-of-line proof: a fire-and-forget Event (contract maxOutputs = 0)
    // is still queueable and runs normally - no poison envelope in front.
    bool eventRan = false;
    bus.registerHandler( "test:evt", EnvelopeKind::Event,
                         [&eventRan]( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) { eventRan = true; },
                         std::string{ "block:B" }, std::string{ "test:bus" }, std::nullopt,
                         PayloadSchema::EventValue );
    auto event = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Event, "test:evt",
                               "block:B", "test:bus" );
    event.payload = EventValuePayload{ PropertyValue{ 1u } };
    CHECK( bus.submit( event ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );
    const auto pumped = bus.pumpOne();
    CHECK( pumped.handled );
    CHECK( eventRan );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );

    // Temporary full (outboundCapacity >= maxOutputs, B currently full):
    // submit stays successful, pumpOne reports backpressure, the envelope
    // stays pending and runs normally after a drain (retry semantics).
    CommunicationRuntime tempBus{ 4, 1 };
    int tempEffects = 0;
    tempBus.registerHandler( "test:temp", EnvelopeKind::Command,
                             [&tempBus, &tempEffects]( const CommunicationEnvelope &cause,
                                                       std::vector<CommunicationEnvelope> &replies ) {
        ++tempEffects;
        replies.push_back( tempBus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    // Fill B with a reply from a first command.
    auto first = baseEnvelope( tempBus.nextMessageId(), EnvelopeKind::Command, "test:temp",
                               "world:state", "core:world" );
    CHECK( tempBus.submit( first ) );
    (void)tempBus.pumpOne();
    CHECK_EQ( tempBus.pendingOutbound(), std::size_t{ 1 } ); // B full
    // The second command submits fine (capacity 1 >= maxOutputs 1) ...
    auto second = baseEnvelope( tempBus.nextMessageId(), EnvelopeKind::Command, "test:temp",
                                "world:state", "core:world" );
    CHECK( tempBus.submit( second ) );
    CHECK_EQ( tempBus.pendingInbound(), std::size_t{ 1 } );
    // ... pumpOne reports temporary backpressure, nothing runs ...
    CHECK( rejected( [&] { (void)tempBus.pumpOne(); } ) );
    CHECK_EQ( tempEffects, 1 );
    CHECK_EQ( tempBus.pendingInbound(), std::size_t{ 1 } );
    // ... after the drain it runs normally.
    CHECK( tempBus.nextOutput().has_value() );
    (void)tempBus.pumpOne();
    CHECK_EQ( tempEffects, 2 );
    CHECK_EQ( tempBus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( tempBus.pendingOutbound(), std::size_t{ 1 } );
    CHECK( tempBus.nextOutput().has_value() );
    CHECK( !tempBus.nextOutput().has_value() );
}

// =====================================================================
// MAJOR 1: async pump is not reentrant
// =====================================================================

TEST_CASE( m03_async_pump_is_not_reentrant )
{
    // MAJOR 1: while pumpOne() is active, a handler (or trace sink) that
    // tries to pump again gets a defined CommunicationError BEFORE touching
    // A/B - no nested handler execution, no nested consumption, no B change.
    // The outer flow completes normally and commits its reply exactly once.
    CommunicationRuntime bus{ 8, 1 };
    bool nestedRejected = false;
    std::string nestedError;
    bus.registerHandler( "test:outer", EnvelopeKind::Command,
                         [&bus, &nestedRejected, &nestedError]( const CommunicationEnvelope &cause,
                                                                std::vector<CommunicationEnvelope> &replies ) {
        try
        {
            (void)bus.pumpOne(); // reentrant attempt from within the handler
        }
        catch( const CommunicationError &error )
        {
            nestedRejected = true;
            nestedError = error.what();
        }
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.registerHandler( "test:inner", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const auto outer = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:outer", "world:state", "core:world" );
    const auto inner = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:inner", "world:state", "core:world" );
    CHECK( bus.submit( outer ) );
    CHECK( bus.submit( inner ) );

    const auto result = bus.pumpOne(); // processes outer
    CHECK( result.handled );
    CHECK( nestedRejected );                                  // nested pump rejected
    CHECK( nestedError.find( "reentrant" ) != std::string::npos );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );       // inner stays pending
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );      // outer reply committed once
    const auto outerReply = bus.nextOutput();
    CHECK( outerReply.has_value() );
    if( outerReply )
    {
        CHECK( outerReply->kind == EnvelopeKind::Reply );
        if( outerReply->correlationId )
            CHECK_EQ( *outerReply->correlationId, outer.messageId );
    }
    CHECK( !bus.nextOutput().has_value() );                   // no silent drop / duplicate

    // After the drain, the normal pump processes the inner message and its
    // reply appears exactly once.
    const auto innerResult = bus.pumpOne();
    CHECK( innerResult.handled );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );
    const auto innerReply = bus.nextOutput();
    CHECK( innerReply.has_value() );
    if( innerReply )
    {
        CHECK( innerReply->kind == EnvelopeKind::Reply );
        if( innerReply->correlationId )
            CHECK_EQ( *innerReply->correlationId, inner.messageId );
    }
    CHECK( !bus.nextOutput().has_value() );
}

TEST_CASE( m03_async_pump_all_is_not_reentrant )
{
    // The same guard covers a nested pumpAll() from within a handler: the
    // nested attempt is rejected, nothing is consumed or delivered by it,
    // and the outer flow completes.
    CommunicationRuntime bus{ 8, 8 };
    bool nestedRejected = false;
    bus.registerHandler( "test:outer", EnvelopeKind::Command,
                         [&bus, &nestedRejected]( const CommunicationEnvelope &cause,
                                                  std::vector<CommunicationEnvelope> &replies ) {
        try
        {
            bus.pumpAll(); // reentrant attempt from within the handler
        }
        catch( const CommunicationError & )
        {
            nestedRejected = true;
        }
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.registerHandler( "test:inner", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const auto outer = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:outer", "world:state", "core:world" );
    const auto inner = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:inner", "world:state", "core:world" );
    CHECK( bus.submit( outer ) );
    CHECK( bus.submit( inner ) );
    (void)bus.pumpOne();
    CHECK( nestedRejected );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } ); // inner untouched by the nested attempt
    (void)bus.pumpOne();                                // normal later pump handles it
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 2 } ); // both replies committed, none lost
}

// =====================================================================
// CONTRACT CLEANUP: the runtime trace shows only accepted bus outputs
// =====================================================================

TEST_CASE( m03_runtime_trace_shows_only_accepted_bus_outputs )
{
    // The production runtime trace records the accepted cause and only
    // CONTRACT-ACCEPTED outputs. A contract-invalid reply (here: wrong
    // correlationId) never appears as a successful bus output, although the
    // handler produced it.
    CommunicationRuntime bus{ 8, 8 };
    std::vector<std::string> traced;
    bus.setTraceSink( [&]( const CommunicationEnvelope &env ) {
        traced.push_back( std::to_string( env.messageId ) + ":" + env.action );
    } );
    bus.registerHandler( "test:badcorr", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        CommunicationEnvelope reply;
        reply.messageId = bus.nextMessageId();
        reply.kind = EnvelopeKind::Reply;
        reply.sender = "world:state";
        reply.receiver = "player:1";
        reply.context = "core:world";
        reply.action = "test:badcorr";
        reply.payload = CommandResultPayload{ true, {}, std::nullopt };
        reply.replyTo = "world:state";
        reply.correlationId = cause.messageId + 1u; // structurally valid, contract-invalid
        replies.push_back( reply );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    auto env = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:badcorr",
                             "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.dispatch( env ); } ) );
    // Only the accepted cause is traced - the contract-invalid reply is NOT.
    CHECK_EQ( traced.size(), std::size_t{ 1 } );
    if( !traced.empty() )
        CHECK_EQ( traced[0], std::to_string( env.messageId ) + ":test:badcorr" );

    // Same boundary on the async path.
    traced.clear();
    auto asyncEnv = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:badcorr",
                                  "world:state", "core:world" );
    CHECK( bus.submit( asyncEnv ) );
    CHECK( rejected( [&] { (void)bus.pumpOne(); } ) );
    CHECK_EQ( traced.size(), std::size_t{ 1 } ); // accepted cause only
    if( !traced.empty() )
        CHECK_EQ( traced[0], std::to_string( asyncEnv.messageId ) + ":test:badcorr" );

    // A contract-valid flow traces the cause AND the accepted reply.
    traced.clear();
    bus.registerHandler( "test:good", EnvelopeKind::Query,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::Query );
    auto good = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query, "test:good",
                              "world:state", "core:world" );
    good.payload = QueryPayload{ "count" };
    const auto result = bus.dispatch( good );
    CHECK( result.handled );
    CHECK_EQ( traced.size(), std::size_t{ 2 } );
    if( traced.size() >= 2 )
    {
        CHECK_EQ( traced[0], std::to_string( good.messageId ) + ":test:good" );
        CHECK_EQ( traced[1], std::to_string( good.messageId + 1 ) + ":test:good" );
    }
}

// =====================================================================
// MAJOR: the runtime trace is purely observational (never influences
// the bus semantics)
// =====================================================================

TEST_CASE( m03_throwing_cause_trace_does_not_disturb_async_pump )
{
    // Pflichttest 1: the trace sink throws for the cause envelope. The
    // failure must be contained: pumpOne() must NOT throw because of the
    // trace, the handler runs exactly once, the input is processed and the
    // output is committed normally.
    CommunicationRuntime bus{ 8, 8 };
    int handlerRuns = 0;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&bus, &handlerRuns]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        ++handlerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.setTraceSink( []( const CommunicationEnvelope &env ) {
        if( env.kind != EnvelopeKind::Reply )
            throw CommunicationError( "contained cause trace failure" );
    } );

    auto command = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:mut",
                                 "world:state", "core:world" );
    CHECK( bus.submit( command ) );
    const auto result = bus.pumpOne(); // must NOT throw because of the trace
    CHECK( result.handled );
    CHECK_EQ( handlerRuns, 1 );                    // handler ran exactly once
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } ); // input consumed
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // output committed
    CHECK( bus.nextOutput().has_value() );
    CHECK_EQ( bus.traceFailureCount(), std::size_t{ 1 } ); // contained, counted
}

TEST_CASE( m03_reentrant_pump_from_trace_sink_is_contained )
{
    // Pflichttest 2: the trace sink itself tries to pump reentrantly. The
    // AsyncPumpGuard rejects the nested pump with CommunicationError, but
    // that exception stays INSIDE the trace observer: the outer pump runs
    // normally, the outer handler runs exactly once, the outer reply is
    // committed exactly once, and the inner envelope stays pending.
    CommunicationRuntime bus{ 8, 1 };
    int outerRuns = 0;
    int innerRuns = 0;
    bus.registerHandler( "test:outer", EnvelopeKind::Command,
                         [&bus, &outerRuns]( const CommunicationEnvelope &cause,
                                             std::vector<CommunicationEnvelope> &replies ) {
        ++outerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.registerHandler( "test:inner", EnvelopeKind::Command,
                         [&bus, &innerRuns]( const CommunicationEnvelope &cause,
                                             std::vector<CommunicationEnvelope> &replies ) {
        ++innerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    // The sink throws a CommunicationError by attempting a reentrant pump;
    // emitTrace() contains it.
    bus.setTraceSink( [&bus]( const CommunicationEnvelope & ) {
        (void)bus.pumpOne(); // reentrant -> CommunicationError -> contained
    } );

    const auto outer = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:outer", "world:state", "core:world" );
    const auto inner = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                     "test:inner", "world:state", "core:world" );
    CHECK( bus.submit( outer ) );
    CHECK( bus.submit( inner ) );
    const auto result = bus.pumpOne(); // outer pump completes normally
    CHECK( result.handled );
    CHECK_EQ( outerRuns, 1 );                             // outer ran exactly once
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );   // inner stays pending
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );  // outer reply committed once
    const auto outerReply = bus.nextOutput();
    CHECK( outerReply.has_value() );
    if( outerReply && outerReply->correlationId )
        CHECK_EQ( *outerReply->correlationId, outer.messageId );
    CHECK( !bus.nextOutput().has_value() );               // no duplication / loss

    // After the drain, a normal later pump processes the inner message.
    const auto innerResult = bus.pumpOne();
    CHECK( innerResult.handled );
    CHECK_EQ( innerRuns, 1 );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );
    const auto innerReply = bus.nextOutput();
    CHECK( innerReply.has_value() );
    if( innerReply && innerReply->correlationId )
        CHECK_EQ( *innerReply->correlationId, inner.messageId );
    // The sink tried to pump on every traced message: cause attempts (outer
    // 2 + inner 1... outer cause + outer reply + inner cause + inner reply).
    CHECK( bus.traceFailureCount() >= std::size_t{ 1 } );
}

TEST_CASE( m03_throwing_output_trace_does_not_fail_delivery )
{
    // Pflichttest 3: the trace sink throws for the OUTPUT. The handler ran,
    // the reply was committed EXACTLY once, pumpOne() stays successful and
    // nextOutput() delivers the reply normally.
    CommunicationRuntime bus{ 8, 8 };
    int handlerRuns = 0;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&bus, &handlerRuns]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        ++handlerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.setTraceSink( []( const CommunicationEnvelope &env ) {
        if( env.kind == EnvelopeKind::Reply )
            throw CommunicationError( "contained output trace failure" );
    } );

    const auto command = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                       "test:mut", "world:state", "core:world" );
    CHECK( bus.submit( command ) );
    const auto result = bus.pumpOne(); // must NOT throw because of the trace
    CHECK( result.handled );
    CHECK_EQ( handlerRuns, 1 );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // reply committed exactly once
    const auto reply = bus.nextOutput();
    CHECK( reply.has_value() );                          // delivered normally
    if( reply )
        CHECK( reply->kind == EnvelopeKind::Reply );
    CHECK( !bus.nextOutput().has_value() );
    CHECK_EQ( bus.traceFailureCount(), std::size_t{ 1 } ); // contained, counted
}

TEST_CASE( m03_throwing_trace_does_not_disturb_sync_dispatch )
{
    // Pflichttest 4: sync dispatch with a throwing trace sink. The handler
    // runs, the valid DispatchResult is returned normally and no trace error
    // leaves dispatch().
    CommunicationRuntime bus{ 8, 8 };
    bool ran = false;
    bus.registerHandler( "test:mut", EnvelopeKind::Query,
                         [&bus, &ran]( const CommunicationEnvelope &cause,
                                       std::vector<CommunicationEnvelope> &replies ) {
        ran = true;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::Query );
    bus.setTraceSink( []( const CommunicationEnvelope & ) {
        throw CommunicationError( "contained sink failure" );
    } );

    auto query = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query, "test:mut",
                               "world:state", "core:world" );
    query.payload = QueryPayload{ "x" };
    const auto result = bus.dispatch( query ); // must NOT throw because of the trace
    CHECK( result.handled );
    CHECK( ran );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } ); // valid result returned
    CHECK_EQ( bus.traceFailureCount(), std::size_t{ 2 } ); // cause + output, both contained
}

// =====================================================================
// MAJOR: the trace sink cannot reentrantly manipulate the runtime
// =====================================================================

TEST_CASE( m03_trace_cannot_steal_outputs )
{
    // Probe A: the sink tries bus.nextOutput() on the reply. The nested
    // consumption is rejected and contained; the reply STAYS in B, pumpOne
    // stays successful and the normal consumer receives it exactly once.
    CommunicationRuntime bus{ 8, 8 };
    int handlerRuns = 0;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&bus, &handlerRuns]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        ++handlerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.setTraceSink( [&bus]( const CommunicationEnvelope &env ) {
        if( env.kind == EnvelopeKind::Reply )
            (void)bus.nextOutput(); // blocked: mutation/consumption from trace sink
    } );

    const auto command = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                       "test:mut", "world:state", "core:world" );
    CHECK( bus.submit( command ) );
    const auto result = bus.pumpOne(); // successful, no trace failure escapes
    CHECK( result.handled );
    CHECK_EQ( handlerRuns, 1 );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // reply NOT stolen
    const auto reply = bus.nextOutput(); // normal consumer gets it exactly once
    CHECK( reply.has_value() );
    if( reply )
    {
        CHECK( reply->kind == EnvelopeKind::Reply );
        if( reply->correlationId )
            CHECK_EQ( *reply->correlationId, command.messageId );
    }
    CHECK( !bus.nextOutput().has_value() );
    CHECK( bus.traceFailureCount() >= std::size_t{ 1 } ); // blocked attempt counted
}

TEST_CASE( m03_trace_cannot_inject_messages )
{
    // Probe B: the sink tries bus.submit(extra) from the cause trace. The
    // injection is rejected and contained; the extra envelope never reaches
    // A, the original message runs normally and pendingInbound reflects only
    // the genuine original work.
    CommunicationRuntime bus{ 8, 8 };
    int handlerRuns = 0;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&bus, &handlerRuns]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        ++handlerRuns;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.setTraceSink( [&bus]( const CommunicationEnvelope &env ) {
        if( env.kind != EnvelopeKind::Reply )
        {
            CommunicationEnvelope extra;
            extra.messageId = 999u;
            extra.kind = EnvelopeKind::Command;
            extra.sender = "player:1";
            extra.receiver = "world:state";
            extra.context = "core:world";
            extra.action = "test:mut";
            (void)bus.submit( extra ); // blocked: mutation from trace sink
        }
    } );

    const auto command = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                       "test:mut", "world:state", "core:world" );
    CHECK( bus.submit( command ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } ); // only the original work
    const auto result = bus.pumpOne();
    CHECK( result.handled );
    CHECK_EQ( handlerRuns, 1 );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } ); // no injected envelope in A
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // only the original reply
    CHECK( bus.nextOutput().has_value() );
    CHECK( !bus.nextOutput().has_value() );
    CHECK( bus.traceFailureCount() >= std::size_t{ 1 } ); // blocked attempt counted
}

TEST_CASE( m03_trace_cannot_dispatch_reentrantly )
{
    // Probe C: the sink tries bus.dispatch(other) from the cause trace. The
    // reentrant dispatch is rejected and contained: the other handler does
    // NOT run, the original handler runs normally and no extra communication
    // is produced.
    CommunicationRuntime bus{ 8, 8 };
    bool originalRan = false;
    bool otherRan = false;
    bus.registerHandler( "test:main", EnvelopeKind::Command,
                         [&bus, &originalRan]( const CommunicationEnvelope &cause,
                                               std::vector<CommunicationEnvelope> &replies ) {
        originalRan = true;
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    bus.registerHandler( "test:other", EnvelopeKind::Command,
                         [&otherRan]( const CommunicationEnvelope &,
                                      std::vector<CommunicationEnvelope> & ) { otherRan = true; },
                         std::string{ "world:state" }, std::string{ "core:world" },
                         std::nullopt, PayloadSchema::None );
    bus.setTraceSink( [&bus]( const CommunicationEnvelope &env ) {
        if( env.kind != EnvelopeKind::Reply )
        {
            CommunicationEnvelope other;
            other.messageId = 777u;
            other.kind = EnvelopeKind::Command;
            other.sender = "player:1";
            other.receiver = "world:state";
            other.context = "core:world";
            other.action = "test:other";
            (void)bus.dispatch( other ); // blocked: mutation from trace sink
        }
    } );

    const auto main = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                    "test:main", "world:state", "core:world" );
    CHECK( bus.submit( main ) );
    const auto result = bus.pumpOne();
    CHECK( result.handled );
    CHECK( originalRan );                 // original handler ran normally
    CHECK( !otherRan );                   // the other handler never ran
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } ); // no extra communication
    CHECK( bus.nextOutput().has_value() );
    CHECK( !bus.nextOutput().has_value() );
    CHECK( bus.traceFailureCount() >= std::size_t{ 1 } ); // blocked attempt counted
}

// =====================================================================
// MAJOR: no raw MessageIdSource access; trace cannot consume ids
// =====================================================================

// (Pflichttest 1) There is deliberately NO public raw MessageIdSource&
// accessor on CommunicationRuntime anymore - message ids come exclusively
// from the guarded nextMessageId() / makeReply(). This is a compile-time /
// API property: `bus.ids()` no longer exists.

TEST_CASE( m03_trace_cannot_consume_message_ids )
{
    // Pflichttest 2: the trace sink tries the ONLY remaining producer call
    // (bus.nextMessageId()). It is rejected, the exception is contained by
    // emitTrace() and counted, and the next id OUTSIDE the trace is exactly
    // the expected next sequence number (no ids were consumed by the trace).
    CommunicationRuntime bus{ 8, 8 };
    bool ran = false;
    bus.registerHandler( "test:mut", EnvelopeKind::Command,
                         [&ran]( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) { ran = true; },
                         std::string{ "world:state" }, std::string{ "core:world" },
                         std::nullopt, PayloadSchema::None );
    bus.setTraceSink( [&bus]( const CommunicationEnvelope & ) {
        (void)bus.nextMessageId(); // blocked: mutation from trace sink
    } );

    auto cause = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:mut",
                               "world:state", "core:world" );
    CHECK_EQ( cause.messageId, std::uint64_t{ 1 } );
    CHECK( bus.dispatch( cause ).handled );
    CHECK( ran );
    CHECK( bus.traceFailureCount() >= std::size_t{ 1 } ); // blocked attempt counted
    CHECK_EQ( bus.nextMessageId(), std::uint64_t{ 2 } );  // the trace consumed nothing

    // Same on the async path.
    auto next = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:mut",
                              "world:state", "core:world" );
    CHECK_EQ( next.messageId, std::uint64_t{ 3 } );
    CHECK( bus.submit( next ) );
    CHECK( bus.pumpOne().handled );
    CHECK_EQ( bus.nextMessageId(), std::uint64_t{ 4 } );
}

TEST_CASE( m03_runtime_reply_ids_stay_in_the_runtime_sequence )
{
    // Pflichttest 3: replies minted via the controlled runtime helper come
    // from the SAME runtime sequence (reply id == cause id + 1) and keep the
    // exact correlation (correlationId == cause.messageId).
    CommunicationRuntime bus{ 8, 8 };
    bus.registerHandler( "test:q", EnvelopeKind::Query,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause,
                                          CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::Query );

    auto query = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query, "test:q",
                               "world:state", "core:world" );
    query.payload = QueryPayload{ "count" };
    CHECK_EQ( query.messageId, std::uint64_t{ 1 } );
    const auto result = bus.dispatch( query );
    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    if( !result.replies.empty() )
    {
        CHECK( result.replies[0].kind == EnvelopeKind::Reply );
        CHECK_EQ( result.replies[0].messageId, std::uint64_t{ 2 } ); // same sequence
        CHECK( result.replies[0].correlationId.has_value() );
        if( result.replies[0].correlationId )
            CHECK_EQ( *result.replies[0].correlationId, std::uint64_t{ 1 } ); // == cause
    }
    CHECK_EQ( bus.nextMessageId(), std::uint64_t{ 3 } ); // sequence continues
}

// =====================================================================
// MAJOR 1: declared handler output contract is strictly enforced
// =====================================================================

TEST_CASE( m03_output_contract_rejects_bad_handler_outputs )
{
    // The slot declares its OutputContract; every produced output must obey
    // it (count, kind, payload schema, correlation with the cause).
    CommunicationRuntime bus{ 8, 8 };

    // 1. maxOutputs = 1, handler produces 2 outputs -> rejected.
    bus.registerHandler( "test:two", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    auto two = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "test:two",
                             "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.dispatch( two ); } ) );
    // ... also on the async queue path (contract check after execution).
    CHECK( bus.submit( two ) );
    CHECK( rejected( [&] { (void)bus.pumpOne(); } ) );

    // 2. Reply expected, handler produces an Event -> rejected.
    bus.registerHandler( "test:wrongkind", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> &replies ) {
        CommunicationEnvelope event;
        event.messageId = bus.nextMessageId();
        event.kind = EnvelopeKind::Event;
        event.sender = "world:state";
        event.receiver = "player:1";
        event.context = "core:world";
        event.action = "test:wrongkind";
        replies.push_back( event );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    auto wrongKind = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                   "test:wrongkind", "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.dispatch( wrongKind ); } ) );

    // 3. Reply expected with CommandResult payload, handler produces
    //    std::monostate -> rejected.
    bus.registerHandler( "test:wrongpayload", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> &replies ) {
        CommunicationEnvelope reply;
        reply.messageId = bus.nextMessageId();
        reply.kind = EnvelopeKind::Reply;
        reply.sender = "world:state";
        reply.receiver = "player:1";
        reply.context = "core:world";
        reply.action = "test:wrongpayload";
        reply.payload = std::monostate{};
        reply.replyTo = "world:state";
        reply.correlationId = 1u;
        replies.push_back( reply );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    auto wrongPayload = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                      "test:wrongpayload", "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.dispatch( wrongPayload ); } ) );

    // 4. Reply with wrong correlationId (!= cause.messageId) -> rejected.
    bus.registerHandler( "test:wrongcorr", EnvelopeKind::Command,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        CommunicationEnvelope reply;
        reply.messageId = bus.nextMessageId();
        reply.kind = EnvelopeKind::Reply;
        reply.sender = "world:state";
        reply.receiver = "player:1";
        reply.context = "core:world";
        reply.action = "test:wrongcorr";
        reply.payload = CommandResultPayload{ true, {}, std::nullopt };
        reply.replyTo = "world:state";
        reply.correlationId = cause.messageId + 1u; // structurally valid, wrong cause
        replies.push_back( reply );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    auto wrongCorr = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                                   "test:wrongcorr", "world:state", "core:world" );
    CHECK( rejected( [&] { (void)bus.dispatch( wrongCorr ); } ) );

    // 5. Event / no-output slot producing ANY output -> rejected.
    bus.registerHandler( "test:evt", EnvelopeKind::Event,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "block:B" }, std::string{ "test:bus" }, std::nullopt,
        PayloadSchema::EventValue );
    auto evt = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Event, "test:evt",
                             "block:B", "test:bus" );
    evt.payload = EventValuePayload{ PropertyValue{ 3u } };
    CHECK( rejected( [&] { (void)bus.dispatch( evt ); } ) );

    // Valid reply passes: kind = Reply, schema = CommandResult,
    // correlationId exactly the cause's messageId.
    bus.registerHandler( "test:good", EnvelopeKind::Query,
                         [&bus]( const CommunicationEnvelope &cause,
                                 std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( bus.makeReply( cause, CommandResultPayload{ true, {}, std::nullopt } ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::Query );
    auto good = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Query, "test:good",
                              "world:state", "core:world" );
    good.payload = QueryPayload{ "count" };
    const auto goodResult = bus.dispatch( good );
    CHECK( goodResult.handled );
    CHECK_EQ( goodResult.replies.size(), std::size_t{ 1 } );
    if( !goodResult.replies.empty() )
    {
        CHECK( goodResult.replies[0].kind == EnvelopeKind::Reply );
        CHECK( goodResult.replies[0].correlationId.has_value() );
        if( goodResult.replies[0].correlationId )
            CHECK_EQ( *goodResult.replies[0].correlationId, good.messageId );
    }
}

// =====================================================================
// FINDING 4: registrations are atomic (no partial state after failure)
// =====================================================================

TEST_CASE( m03_failed_registrations_leave_no_partial_state )
{
    CommunicationRuntime bus{ 64, 64 };
    bus.declareSignal( "test:sig", EnvelopeKind::Event, PayloadSchema::EventValue );
    bus.registerAction( "test:act", []( const CommunicationEnvelope &,
                                        std::vector<CommunicationEnvelope> & ) {} );
    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 0 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 0 } );

    // Failing registrations leave ALL counts unchanged (nothing was mutated).
    // invalid context
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "receiver:a" },
                          std::string{ "nocolon" }, std::nullopt, PayloadSchema::EventValue,
                          "test:act" );
    } ) );
    // invalid capability
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::nullopt, std::nullopt,
                          std::string{ "nocolon" }, PayloadSchema::EventValue, "test:act" );
    } ) );
    // unknown action
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::nullopt, std::nullopt,
                          std::nullopt, PayloadSchema::EventValue, "test:missing" );
    } ) );
    // undeclared signal
    CHECK( rejected( [&] {
        bus.registerSlot( "test:ghost", EnvelopeKind::Command, std::nullopt, std::nullopt,
                          std::nullopt, PayloadSchema::None, "test:act" );
    } ) );
    // invalid output contract kind (forged enum) and schema
    const auto badKind = static_cast<EnvelopeKind>( static_cast<std::uint8_t>( 99u + bus.signalCount() ) ); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::nullopt, std::nullopt,
                          std::nullopt, PayloadSchema::EventValue, "test:act",
                          OutputContract{ 1, badKind, PayloadSchema::CommandResult, true } );
    } ) );
    CHECK_EQ( bus.slotCount(), std::size_t{ 0 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 0 } );

    // A non-namespaced receiver is a PLAIN logical id (envelope contract:
    // sender/receiver are logical ids, only context/action/capability are
    // namespaced) - it registers fine.
    CHECK( !rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "plainreceiver" },
                          std::nullopt, std::nullopt, PayloadSchema::EventValue, "test:act" );
    } ) );
    CHECK_EQ( bus.slotCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 1 } );
    // ...and the exact duplicate then fails without touching the counts.
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "plainreceiver" },
                          std::nullopt, std::nullopt, PayloadSchema::EventValue, "test:act" );
    } ) );
    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 1 } );

    // The convenience registerHandler is equally atomic: invalid context,
    // invalid capability, empty handler and the invalid "Any" schema all
    // leave the registries untouched.
    CHECK( rejected( [&] {
        bus.registerHandler( "test:h", EnvelopeKind::Command,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::string{ "nocolon" }, std::nullopt,
                             PayloadSchema::None );
    } ) );
    CHECK( rejected( [&] {
        bus.registerHandler( "test:h", EnvelopeKind::Command,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::nullopt, std::string{ "nocolon" },
                             PayloadSchema::None );
    } ) );
    CHECK( rejected( [&] {
        bus.registerHandler( "test:h", EnvelopeKind::Command, {},
                             std::nullopt, std::nullopt, std::nullopt, PayloadSchema::None );
    } ) );
    CHECK( rejected( [&] {
        bus.registerHandler( "test:h", EnvelopeKind::Command,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::nullopt, std::nullopt, PayloadSchema::Any );
    } ) ); // Any is not a valid concrete schema for a signal
    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 1 } );

    // A good convenience registration commits all four structures...
    CHECK( !rejected( [&] {
        bus.registerHandler( "test:ok", EnvelopeKind::Event,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::nullopt, std::nullopt,
                             PayloadSchema::EventValue );
    } ) );
    CHECK_EQ( bus.signalCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 2 } );
    // ...and the duplicate is loud and state-less.
    CHECK( rejected( [&] {
        bus.registerHandler( "test:ok", EnvelopeKind::Event,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::nullopt, std::nullopt,
                             PayloadSchema::EventValue );
    } ) );
    CHECK_EQ( bus.signalCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 2 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 2 } );
}

// =====================================================================
// FINDING 5: PayloadSchema enum values are validated centrally
// =====================================================================

TEST_CASE( m03_payload_schema_rejects_unknown_enum_values )
{
    CommunicationRuntime bus{ 8, 8 };
    bus.declareSignal( "test:sig", EnvelopeKind::Event, PayloadSchema::EventValue );
    bus.registerAction( "test:act", []( const CommunicationEnvelope &,
                                        std::vector<CommunicationEnvelope> & ) {} );

    // A forged out-of-range enum value - computed at runtime so the analyzer
    // cannot fold it into a constant path (same pattern as the fabricated
    // kind regression in TestRouter.cpp).
    const auto forged = static_cast<PayloadSchema>( static_cast<std::uint8_t>( 99u + bus.nextMessageId() % 2u ) ); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    CHECK( rejected( [&] { bus.declareSignal( "test:bad", EnvelopeKind::Command, forged ); } ) );
    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::nullopt, std::nullopt,
                          std::nullopt, forged, "test:act" );
    } ) );
    CHECK( rejected( [&] {
        bus.registerHandler( "test:bad", EnvelopeKind::Command,
                             []( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) {},
                             std::nullopt, std::nullopt, std::nullopt, forged );
    } ) );
    CHECK_EQ( bus.signalCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.actionCount(), std::size_t{ 1 } );
    CHECK_EQ( bus.slotCount(), std::size_t{ 0 } );
    CHECK_EQ( bus.routeCount(), std::size_t{ 0 } );
}

// =====================================================================
// FINDING 6: Event fire-and-forget + optional causal correlation
// =====================================================================

TEST_CASE( m03_event_fire_and_forget_is_enforced )
{
    // An Event stays fire-and-forget (no Reply expected, no Reply produced) -
    // but it MAY carry an optional correlationId expressing causality
    // (e.g. timer message 100 -> Event 101 with correlationId = 100).
    auto causal = baseEnvelope( 1, EnvelopeKind::Event, "test:peer.change_color",
                                "block:B", "test:bus" );
    causal.correlationId = 100u;
    CHECK( !rejected( [&] { validateEnvelope( causal ); } ) ); // valid causal link

    auto broken = causal;
    broken.correlationId = 0u; // present correlation must be > 0
    CHECK( rejected( [&] { validateEnvelope( broken ); } ) );

    // A handler that fabricates replies from an Event is a contract violation
    // at the router boundary.
    Fixture f;
    f.router.registerHandler( "test:bad", EnvelopeKind::Event,
                              [&]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( makeReply( cause, CommandResultPayload{ true, {}, std::nullopt },
                                      f.ids ) );
    },
        std::string{ "block:B" }, std::string{ "test:bus" } );
    auto badEvent = baseEnvelope( f.ids.next(), EnvelopeKind::Event, "test:bad",
                                  "block:B", "test:bus" );
    CHECK( rejected( [&] { (void)f.router.dispatch( badEvent ); } ) );

    // A quiet Event dispatch passes fire-and-forget (no replies).
    bool eventRan = false;
    f.router.registerHandler( "Event:good", EnvelopeKind::Event,
                              [&eventRan]( const CommunicationEnvelope &,
                                           std::vector<CommunicationEnvelope> & ) {
        eventRan = true;
    },
        std::string{ "block:B" }, std::string{ "test:bus" } );
    auto goodEvent = baseEnvelope( f.ids.next(), EnvelopeKind::Event, "Event:good",
                                   "block:B", "test:bus" );
    const auto result = f.router.dispatch( goodEvent );
    CHECK( result.handled );
    CHECK( eventRan );
    CHECK( result.replies.empty() );
}

// =====================================================================
// FINDING 7: submit() is a validated bus boundary
// =====================================================================

TEST_CASE( m03_submit_rejects_invalid_envelopes_at_the_boundary )
{
    CommunicationRuntime bus{ 4, 4 };
    bus.registerHandler( "test:q", EnvelopeKind::Command,
                         []( const CommunicationEnvelope &,
                             std::vector<CommunicationEnvelope> & ) {},
                         std::string{ "world:state" }, std::string{ "core:world" },
                         std::nullopt, PayloadSchema::None );

    // messageId 0
    auto env = baseEnvelope( 0, EnvelopeKind::Command, "test:q", "world:state", "core:world" );
    env.payload = std::monostate{};
    CHECK( rejected( [&] { (void)bus.submit( env ); } ) );
    // invalid namespace
    env = baseEnvelope( 1, EnvelopeKind::Command, "test:q", "world:state", "nocolon" );
    env.payload = std::monostate{};
    CHECK( rejected( [&] { (void)bus.submit( env ); } ) );
    // invalid kind / payload-vs-signal contract (signal declares None, payload
    // is a place payload)
    env = baseEnvelope( 2, EnvelopeKind::Command, "test:q", "world:state", "core:world" );
    env.payload = BlockPlacePayload{ 1u };
    CHECK( rejected( [&] { (void)bus.submit( env ); } ) );
    // The queue stayed untouched after every reject.
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    // A valid envelope passes the boundary and is staged.
    env = baseEnvelope( 3, EnvelopeKind::Command, "test:q", "world:state", "core:world" );
    env.payload = std::monostate{};
    CHECK( bus.submit( env ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 1 } );
}

// =====================================================================
// FINDING 8 follows the registerHandler API: the payload schema is MANDATORY
// (no silently-invalid default); every registration above passes it
// explicitly, including the convenience path.
// =====================================================================

TEST_CASE( m03_register_handler_requires_a_concrete_schema )
{
    // The schema is a required positional argument; no default can silently
    // produce an invalid registration. (Compile-time contract; runtime checks
    // the Any case strictly, proven in the atomic-registration test above.)
    CommunicationRuntime bus{ 8, 8 };
    bool ran = false;
    bus.registerHandler( "test:mandatory", EnvelopeKind::Command,
                         [&ran]( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) { ran = true; },
                         std::nullopt, std::nullopt, std::nullopt, PayloadSchema::None );
    auto env = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command,
                             "test:mandatory", "machine:x", "core:world" );
    CHECK( bus.dispatch( env ).handled );
    CHECK( ran );
}

// =====================================================================
// Existing M03 Round-1 candidate tests (kept)
// =====================================================================

TEST_CASE( m03_event_routes_to_its_addressed_handler )
{
    Fixture f;
    PropertyValue received{ std::uint32_t{ 0 } };
    bool eventRan = false;
    f.router.registerHandler( "test:peer.change_color", EnvelopeKind::Event,
                              [&]( const CommunicationEnvelope &env,
                                   std::vector<CommunicationEnvelope> & ) {
        eventRan = true;
        if( const auto *v = std::get_if<EventValuePayload>( &env.payload ) )
            received = v->value;
    },
        std::string{ "block:B" }, std::string{ "test:bus" } );

    auto event = baseEnvelope( f.ids.next(), EnvelopeKind::Event, "test:peer.change_color",
                               "block:B", "test:bus" );
    event.payload = EventValuePayload{ PropertyValue{ 7u } };
    const auto result = f.router.dispatch( event );
    CHECK( result.handled );
    CHECK( eventRan );
    CHECK_EQ( std::get<std::uint32_t>( received ), std::uint32_t{ 7 } );

    // A wrongly addressed Event (no route for this action) is loud.
    auto wrong = baseEnvelope( f.ids.next(), EnvelopeKind::Event, "test:peer.change_color",
                               "block:other", "test:bus" );
    CHECK( rejected( [&] { (void)f.router.dispatch( wrong ); } ) );
    CHECK( result.replies.empty() ); // events are fire-and-forget: no reply
}

TEST_CASE( m03_query_reply_correlates_exactly )
{
    // M03 Query -> Reply correlation: the handler answers through makeReply,
    // so the Reply ALWAYS carries the cause's messageId.
    Fixture f;
    f.router.registerHandler( "test:query.block", EnvelopeKind::Query,
                              [&]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        replies.push_back( makeReply( cause,
                                      CommandResultPayload{ true, {}, PropertyValue{ 42u } },
                                      f.ids ) );
    },
        std::string{ "world:state" }, std::string{ "core:world" } );

    auto query = baseEnvelope( f.ids.next(), EnvelopeKind::Query, "test:query.block",
                               "world:state", "core:world" );
    query.payload = QueryPayload{ "count" };
    const auto result = f.router.dispatch( query );

    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );
    const auto &reply = result.replies[0];
    CHECK( reply.kind == EnvelopeKind::Reply );
    CHECK( reply.correlationId.has_value() );
    CHECK_EQ( *reply.correlationId, query.messageId );
    CHECK( reply.receiver == query.sender );
    validateEnvelope( reply ); // the reply is valid by construction
    const auto *answer = std::get_if<CommandResultPayload>( &reply.payload );
    CHECK( answer != nullptr );
    if( answer )
    {
        CHECK( answer->ok );
        CHECK( answer->value.has_value() );
        if( answer->value )
            CHECK_EQ( std::get<std::uint32_t>( *answer->value ), std::uint32_t{ 42 } );
    }
}

TEST_CASE( m03_typed_payloads_transport_by_value )
{
    auto env = baseEnvelope( 1, EnvelopeKind::Query, "test:q", "world:state", "core:world" );
    env.payload = QueryPayload{ "count" };
    validateEnvelope( env );
    const auto *q = std::get_if<QueryPayload>( &env.payload );
    CHECK( q != nullptr );
    if( q )
        CHECK_EQ( q->property, "count" );

    auto event = baseEnvelope( 2, EnvelopeKind::Event, "test:e", "block:B", "test:bus" );
    event.payload = EventValuePayload{ PropertyValue{ 1.5f } };
    validateEnvelope( event );
    const auto *e = std::get_if<EventValuePayload>( &event.payload );
    CHECK( e != nullptr );
    if( e )
        CHECK( std::holds_alternative<float>( e->value ) );

    // A non-matching payload type never makes it to a handler by the bus
    // boundary: the signal/slot schema is checked before execution.
    CommunicationRuntime bus{ 8, 8 };
    bool ran = false;
    bus.registerHandler( "test:typed", EnvelopeKind::Event,
                         [&ran]( const CommunicationEnvelope &,
                                 std::vector<CommunicationEnvelope> & ) { ran = true; },
                         std::string{ "block:B" }, std::string{ "test:bus" },
                         std::nullopt, PayloadSchema::EventValue );
    auto mismatched = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Event, "test:typed",
                                    "block:B", "test:bus" );
    mismatched.payload = std::monostate{}; // wrong schema for this signal
    CHECK( rejected( [&] { (void)bus.dispatch( mismatched ); } ) );
    CHECK( !ran );
}

TEST_CASE( m03_capability_is_a_routing_dimension )
{
    // Routing capability: the envelope's capability string selects the slot
    // that offers that capability - a routing request, NOT an authorization.
    // Authorization is tested separately (registered grants).
    Fixture f;
    bool guardedRan = false;
    f.router.registerHandler( "core:mutate", EnvelopeKind::Command,
                              [&]( const CommunicationEnvelope &,
                                   std::vector<CommunicationEnvelope> & ) { guardedRan = true; },
                              std::string{ "world:state" }, std::string{ "core:world" },
                              std::string{ "world:mutate" } );

    auto ok = baseEnvelope( f.ids.next(), EnvelopeKind::Command, "core:mutate",
                            "world:state", "core:world" );
    ok.capability = std::string{ "world:mutate" };
    CHECK( f.router.dispatch( ok ).handled );
    CHECK( guardedRan );

    auto noCap = baseEnvelope( f.ids.next(), EnvelopeKind::Command, "core:mutate",
                               "world:state", "core:world" );
    CHECK( rejected( [&] { (void)f.router.dispatch( noCap ); } ) );

    auto wrongCap = baseEnvelope( f.ids.next(), EnvelopeKind::Command, "core:mutate",
                                  "world:state", "core:world" );
    wrongCap.capability = std::string{ "other:capability" };
    CHECK( rejected( [&] { (void)f.router.dispatch( wrongCap ); } ) );
}

TEST_CASE( m03_capability_specific_route_beats_generic )
{
    // Within the exact receiver/context tier, the capability-specific route
    // wins over the generic one - deterministic routing.
    Fixture f;
    std::string chosen;
    const auto record = [&chosen]( std::string tag ) {
        return [&chosen, tag = std::move( tag )]( const CommunicationEnvelope &,
                                                  std::vector<CommunicationEnvelope> & ) {
            chosen = tag;
        };
    };
    f.router.registerHandler( "test:duo", EnvelopeKind::Command, record( "generic" ),
                              std::string{ "machine:x" }, std::string{ "core:world" } );
    f.router.registerHandler( "test:duo", EnvelopeKind::Command, record( "specific" ),
                              std::string{ "machine:x" }, std::string{ "core:world" },
                              std::string{ "core:special" } );

    auto tagged = baseEnvelope( f.ids.next(), EnvelopeKind::Command, "test:duo",
                                "machine:x", "core:world" );
    tagged.capability = std::string{ "core:special" };
    (void)f.router.dispatch( tagged );
    CHECK_EQ( chosen, "specific" );

    auto untagged = baseEnvelope( f.ids.next(), EnvelopeKind::Command, "test:duo",
                                  "machine:x", "core:world" );
    (void)f.router.dispatch( untagged );
    CHECK_EQ( chosen, "generic" );
}

TEST_CASE( m03_authorization_comes_from_registered_grants_not_envelope_string )
{
    // Authorization capability comes from REGISTERED grants, never from a
    // capability string the sender writes into its own envelope.
    CommunicationRuntime bus{ 8, 8 };
    bool guardedRan = false;
    bus.registerHandler( "core:mutate", EnvelopeKind::Command,
                         [&guardedRan]( const CommunicationEnvelope &,
                                        std::vector<CommunicationEnvelope> & ) { guardedRan = true; },
                         std::string{ "world:state" }, std::string{ "core:world" },
                         std::string{ "world:mutate" }, PayloadSchema::None );

    auto env = baseEnvelope( bus.nextMessageId(), EnvelopeKind::Command, "core:mutate",
                             "world:state", "core:world" );
    env.capability = std::string{ "world:mutate" };

    // Self-asserted capability without a grant: rejected at the boundary
    // (both on the sync and on the async path).
    CHECK( rejected( [&] { (void)bus.dispatch( env ); } ) );
    CHECK( rejected( [&] { (void)bus.submit( env ); } ) );
    CHECK_EQ( bus.pendingInbound(), std::size_t{ 0 } );
    CHECK( !guardedRan );

    bus.grantCapability( "player:1", "world:mutate" );
    CHECK( bus.dispatch( env ).handled );
    CHECK( guardedRan );

    env.sender = "player:2";
    CHECK( rejected( [&] { (void)bus.dispatch( env ); } ) );

    CHECK( rejected( [&] { bus.grantCapability( "player:1", "notnamespaced" ); } ) );
    CHECK( rejected( [&] { bus.grantCapability( "", "world:mutate" ); } ) );
}

TEST_CASE( m03_incompatible_slot_schema_fails_at_registration )
{
    CommunicationRuntime bus{ 8, 8 };
    bus.declareSignal( "test:sig", EnvelopeKind::Event, PayloadSchema::EventValue );
    bus.registerAction( "test:react", []( const CommunicationEnvelope &,
                                          std::vector<CommunicationEnvelope> & ) {} );

    CHECK( rejected( [&] {
        bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "block:B" },
                          std::string{ "test:bus" }, std::nullopt, PayloadSchema::BlockPlace,
                          "test:react" );
    } ) );
    bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "block:B" },
                      std::string{ "test:bus" }, std::nullopt, PayloadSchema::EventValue,
                      "test:react" );
    bus.registerSlot( "test:sig", EnvelopeKind::Event, std::string{ "block:C" },
                      std::string{ "test:bus" }, std::nullopt, PayloadSchema::Any,
                      "test:react" );

    CHECK( rejected( [&] { bus.declareSignal( "test:sig", EnvelopeKind::Event,
                                              PayloadSchema::Any ); } ) );
    CHECK( rejected( [&] { bus.declareSignal( "test:sig", EnvelopeKind::Event,
                                              PayloadSchema::Query ); } ) );
}

// =====================================================================
// M03 Round 1 production proof: place/remove through the runtime bus
// =====================================================================

TEST_CASE( m03_runtime_place_remove_flow_proves_the_production_bus )
{
    BlockRegistry blocks;
    SidecarRegistry sidecars;
    PrototypeRegistry prototypes;
    ChunkManager chunks;
    BlockIdTable idTable;
    MemoryPersistenceSink sink;
    WorldState state( chunks, idTable, sidecars, prototypes );
    CommunicationRuntime bus{ 8, 8 };
    state.setPersistenceSink( &sink );

    BlockDef air;
    air.id = "core:air";
    air.displayName = "Air";
    blocks.insert( air );
    BlockDef stone;
    stone.id = "core:stone";
    stone.displayName = "Stone";
    blocks.insert( stone );
    idTable = BlockIdTable( blocks );
    const std::uint16_t stoneId = idTable.indexOf( "core:stone" );
    registerBlockCommandHandlers( bus, state );

    std::vector<std::string> traced;
    bus.setTraceSink( [&]( const CommunicationEnvelope &env ) {
        traced.push_back( std::to_string( env.messageId ) + ":" + env.action );
    } );

    const BlockAddress placement = fromOriginOffset( 2, 3, 2 );
    chunks.loadChunk( placement.chunk );

    auto command = [&bus]( const std::string &action, Payload payload,
                           const WorldStateTarget &target ) {
        CommunicationEnvelope env;
        env.messageId = bus.nextMessageId();
        env.kind = EnvelopeKind::Command;
        env.sender = "player:1";
        env.receiver = "world:state";
        env.context = "core:world";
        env.action = action;
        env.target = target;
        env.payload = std::move( payload );
        return env;
    };

    // sync place: the reply is delivered in the result, B stays untouched.
    const auto placeCommand = command( ACTION_BLOCK_PLACE, BlockPlacePayload{ stoneId },
                                       WorldStateTarget( placement ) );
    const auto placed = bus.dispatch( placeCommand );
    CHECK( placed.handled );
    CHECK_EQ( placed.replies.size(), std::size_t{ 1 } );
    const auto *placedReply = std::get_if<CommandResultPayload>( &placed.replies[0].payload );
    CHECK( placedReply != nullptr );
    if( placedReply )
        CHECK( placedReply->ok );
    const auto placedBlock = state.blockAt( placement );
    CHECK( placedBlock.has_value() );
    if( placedBlock )
        CHECK_EQ( *placedBlock, stoneId );
    CHECK( sink.isDirty( placement.chunk ) );
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 0 } ); // one delivery route
    if( !placed.replies.empty() )
    {
        const auto &correlation = placed.replies[0].correlationId;
        if( correlation.has_value() )
            CHECK_EQ( *correlation, placeCommand.messageId );
    }

    // sync remove through the same bus
    const auto removed = bus.dispatch(
        command( ACTION_BLOCK_REMOVE, std::monostate{}, WorldStateTarget( placement ) ) );
    CHECK( removed.handled );
    const auto *removedReply = std::get_if<CommandResultPayload>( &removed.replies[0].payload );
    CHECK( removedReply != nullptr );
    if( removedReply )
        CHECK( removedReply->ok );
    const auto removedBlock = state.blockAt( placement );
    if( removedBlock )
        CHECK_EQ( *removedBlock, 0u );

    // schema boundary (sync path): a place command with the wrong payload
    // type never reaches the handler.
    auto wrongPayload = command( ACTION_BLOCK_PLACE, std::monostate{},
                                 WorldStateTarget( placement ) );
    CHECK( rejected( [&] { (void)bus.dispatch( wrongPayload ); } ) );

    // The ASYNC path delivers the same place command's reply through queue B.
    CHECK( bus.submit( command( ACTION_BLOCK_PLACE, BlockPlacePayload{ stoneId },
                                WorldStateTarget( placement ) ) ) );
    const auto asyncResult = bus.pumpOne();
    CHECK( asyncResult.handled );
    CHECK_EQ( asyncResult.replies.size(), std::size_t{ 0 } ); // outputs only in B
    CHECK_EQ( bus.pendingOutbound(), std::size_t{ 1 } );
    const auto asyncReply = bus.nextOutput();
    CHECK( asyncReply.has_value() );
    if( asyncReply )
    {
        CHECK( asyncReply->kind == EnvelopeKind::Reply );
        CHECK( asyncReply->action == ACTION_BLOCK_PLACE );
    }
    CHECK( !bus.nextOutput().has_value() );

    // trace saw the envelopes and their produced replies, in order.
    CHECK( traced.size() >= 2u );
    if( traced.size() >= 2u )
    {
        CHECK( traced[0].find( ACTION_BLOCK_PLACE ) != std::string::npos );
        CHECK( traced[1].find( ACTION_BLOCK_PLACE ) != std::string::npos );
    }
}

int main() { return test::runAll(); }