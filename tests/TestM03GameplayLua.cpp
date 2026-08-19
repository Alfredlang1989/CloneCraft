#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/communication/SchedulerClock.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/scripting/GameplayLuaRuntime.h"
#include "world/state/WorldState.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace
{
    using namespace world;
    using namespace world::communication;
    using namespace world::scripting;

    using Time = SchedulerClock::Time;

    /** Deterministic fake clock with the same pending-safe interrupt contract
     *  as the production clock (Round 2). */
    class TestSchedulerClock final : public SchedulerClock
    {
    public:
        Time now() const override
        {
            std::lock_guard<std::mutex> lock( mMutex );
            return mNow;
        }

        void waitUntil( Time until ) override
        {
            std::unique_lock<std::mutex> lock( mMutex );
            if( mPendingInterrupt )
            {
                mPendingInterrupt = false;
                return;
            }
            while( mNow < until && !mPendingInterrupt )
                mCv.wait( lock );
            mPendingInterrupt = false;
        }

        void interrupt() override
        {
            {
                std::lock_guard<std::mutex> lock( mMutex );
                mPendingInterrupt = true;
            }
            mCv.notify_all();
        }

        void advance( std::chrono::steady_clock::duration delta )
        {
            {
                std::lock_guard<std::mutex> lock( mMutex );
                mNow += delta;
            }
            mCv.notify_all();
        }

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;
        Time mNow{};
        bool mPendingInterrupt = false;
    };

    bool waitFor( const std::function<bool()> &condition, int timeoutMs = 1000 )
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        while( std::chrono::steady_clock::now() < deadline )
        {
            if( condition() )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return condition();
    }

    CommunicationEnvelope makeEnvelope( std::uint64_t id, EnvelopeKind kind,
                                        const std::string &sender, const std::string &receiver,
                                        const std::string &action, Payload payload = Payload{},
                                        std::optional<WorldStateTarget> target = std::nullopt )
    {
        CommunicationEnvelope env;
        env.messageId = id;
        env.kind = kind;
        env.sender = sender;
        env.receiver = receiver;
        env.context = "core:test";
        env.action = action;
        env.payload = std::move( payload );
        env.target = target;
        return env;
    }

    struct Fixture
    {
        TestSchedulerClock clock;
        DelayedMessageScheduler scheduler{ clock, 64 };
        ChunkManager chunks;
        BlockRegistry blocks;
        SidecarRegistry sidecars;
        PrototypeRegistry prototypes;
        BlockIdTable idTable;
        WorldState state{ chunks, idTable, sidecars, prototypes };
        CommunicationRuntime bus;
        std::shared_ptr<GameplayLuaRuntime> lua;

        std::uint16_t stoneId = 0;

        Fixture( std::size_t inboundCap = 64, std::size_t outboundCap = 64 ) :
            bus( inboundCap, outboundCap )
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
            lua = GameplayLuaRuntime::create( bus, scheduler, state );
        }

        std::vector<CommunicationEnvelope> invoke( const std::string &scriptId,
                                                   const std::string &functionName,
                                                   const std::string &principal,
                                                   const CommunicationEnvelope &cause )
        {
            std::vector<CommunicationEnvelope> replies;
            lua->invoke( scriptId, functionName, principal, cause, replies );
            return replies;
        }

        void pumpAll()
        {
            while( bus.pendingInbound() > 0u )
                (void)bus.pumpOne();
        }
    };

    void registerLuaHandler( Fixture &f, const std::string &action, EnvelopeKind kind,
                             const std::string &receiver, PayloadSchema schema,
                             ScriptBinding binding, const std::string &context = "core:test",
                             const std::optional<std::string> &capability = std::nullopt )
    {
        f.bus.registerHandler( action, kind,
                               GameplayLuaRuntime::bridgeHandler( f.lua, std::move( binding ) ),
                               receiver, context, capability, schema );
    }
} // namespace

// ---------------------------------------------------------------------------
// A. Script isolation: script globals never leak into other scripts
// ---------------------------------------------------------------------------

TEST_CASE( m3r3_lua_scripts_have_isolated_environments )
{
    Fixture f;
    f.lua->loadScript( "a", "foo = 123" ); // only visible in a's own _ENV
    f.lua->loadScript( "b",
                       "foo_b = 7\n"
                       "function probe()\n"
                       "  assert( foo == nil, 'global leaked from script a into script b' )\n"
                       "  assert( foo_b == 7 )\n"
                       "end\n" );
    const CommunicationEnvelope cause =
        makeEnvelope( 1, EnvelopeKind::Command, "player:1", "b", "test:probe" );
    std::vector<CommunicationEnvelope> replies;
    f.lua->invoke( "b", "probe", "principal:b", cause, replies ); // must not throw
    CHECK_EQ( replies.size(), std::size_t{ 0 } );
}

// ---------------------------------------------------------------------------
// B. Sandbox: dangerous standard libraries and randomness are absent
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_sandbox_removes_dangerous_libraries )
{
    Fixture f;
    f.lua->loadScript( "sandbox", R"(
        function probe()
            assert( os == nil, "os must not be available" )
            assert( io == nil, "io must not be available" )
            assert( debug == nil, "debug must not be available" )
            assert( package == nil, "package must not be available" )
            assert( require == nil, "require must not be available" )
            assert( dofile == nil, "dofile must not be available" )
            assert( loadfile == nil, "loadfile must not be available" )
            assert( load == nil, "load must not be available" )
            assert( coroutine == nil, "coroutine must not be available" )
            assert( math.random == nil, "math.random must not be available" )
            assert( math.randomseed == nil, "math.randomseed must not be available" )
            assert( type( assert ) == "function" )
            assert( type( error ) == "function" )
            assert( type( ipairs ) == "function" )
            assert( type( pairs ) == "function" )
            assert( type( pcall ) == "function" )
            assert( type( xpcall ) == "function" )
            assert( type( table.concat ) == "function" )
            assert( type( string.format ) == "function" )
            assert( type( math.abs ) == "function" )
            assert( type( utf8.len ) == "function" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "sandbox", "probe", "principal:s", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// C. Instruction budget: infinite loops end in a defined error, state stays
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_instruction_budget_stops_infinite_loops )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );

    // Top-level chunk execution is an invocation too: its budget abort must
    // retain the defined budget-error type/text rather than exposing the
    // private sentinel as an "unknown Lua error".
    bool topLevelCaught = false;
    try
    {
        f.lua->loadScript( "topspin", "while true do end" );
    }
    catch( const GameplayLuaBudgetError &error )
    {
        topLevelCaught = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( topLevelCaught );

    f.lua->loadScript( "spin", "function spin() while true do end end" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "spin", "spin", "principal:p", cause );
    }
    catch( const GameplayLuaError & )
    {
        caught = true;
    }
    CHECK( caught ); // the infinite loop was interrupted, not hung

    // The same runtime remains fully usable afterwards.
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    f.lua->loadScript( "ok", "function goodness() return 42 end" );
    (void)f.invoke( "ok", "goodness", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// D. Owner thread: foreign-thread invocations are rejected loudly
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_foreign_thread_invocation_is_rejected )
{
    Fixture f;
    f.lua->loadScript( "s", "function h(msg) end" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );

    std::atomic<bool> rejected{ false };
    std::thread foreign( [&] {
        std::vector<CommunicationEnvelope> replies;
        try
        {
            f.lua->invoke( "s", "h", "principal:p", cause, replies );
        }
        catch( const GameplayLuaError & )
        {
            rejected = true;
        }
    } );
    foreign.join();
    CHECK( rejected ); // defined rejection, no Lua state touched

    // The owner thread keeps working.
    (void)f.invoke( "s", "h", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// E. Envelope snapshot: complete fields, read-only, C++ message unchanged
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_envelope_snapshot_is_complete_and_read_only )
{
    Fixture f;
    f.lua->loadScript( "snap", R"(
        function probe(msg)
            assert( msg.message_id == "4242", "message_id is an opaque decimal string" )
            assert( msg.kind == "Query" )
            assert( msg.sender == "player:1" )
            assert( msg.receiver == "world:ans" )
            assert( msg.context == "core:test" )
            assert( msg.action == "test:snap" )
            assert( msg.capability == nil )
            assert( msg.reply_to == nil )
            assert( msg.correlation_id == nil )
            assert( msg.target == nil )
            assert( msg.payload.schema == "query" )
            assert( msg.payload.property == "answer" )
            local changed = pcall( function() msg.sender = "evil" end )
            assert( changed == false, "snapshot writes must be rejected" )
            local changedId = pcall( function() msg.message_id = 999 end )
            assert( changedId == false, "snapshot writes must be rejected" )
            return true
        end
    )" );
    CommunicationEnvelope cause = makeEnvelope(
        4242, EnvelopeKind::Query, "player:1", "world:ans", "test:snap",
        Payload{ QueryPayload{ "answer" } } );
    (void)f.invoke( "snap", "probe", "principal:p", cause );
    // The authoritative C++ message is untouched by any Lua attempt.
    CHECK_EQ( cause.sender, "player:1" );
    CHECK_EQ( cause.messageId, std::uint64_t{ 4242 } );
}

// ---------------------------------------------------------------------------
// F. Sender anti-spoof: outbound identity/messageId always come from C++
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_sender_spoofing_is_impossible )
{
    Fixture f;
    std::vector<CommunicationEnvelope> observed;
    f.bus.registerHandler( "test:spoof", EnvelopeKind::Event,
                           [&observed]( const CommunicationEnvelope &env,
                                        std::vector<CommunicationEnvelope> & ) {
        observed.push_back( env );
    },
        "capture", "core:test", std::nullopt, PayloadSchema::None );

    f.lua->loadScript( "spoofer", R"(
        function attempt()
            -- spoofed authoritative fields are rejected by the strict decoder
            local rejected = pcall( function()
                bus.send( { kind="event", receiver="capture", context="core:test",
                            action="test:spoof", payload={schema="none"},
                            sender="admin", message_id=999 } )
            end )
            assert( rejected == false, "sender/message_id spoofing must be rejected" )
            -- a clean spec goes out with the HOST principal and a runtime id
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:spoof", payload={schema="none"} } )
            return true
        end
    )" );
    const CommunicationEnvelope cause =
        makeEnvelope( 1, EnvelopeKind::Command, "player:1", "capture", "test:spoof" );
    (void)f.invoke( "spoofer", "attempt", "principal:alpha", cause );
    f.pumpAll();

    CHECK_EQ( observed.size(), std::size_t{ 1 } );
    if( !observed.empty() )
    {
        CHECK_EQ( observed[0].sender, "principal:alpha" ); // host binding identity
        CHECK( observed[0].messageId > 0u );
        CHECK( observed[0].messageId != 999u );
    }
}

// ---------------------------------------------------------------------------
// G. Lua Event send A -> B routes exactly once via the bus
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_lua_event_send_a_to_b_routes_exactly_once )
{
    Fixture f;
    std::thread::id handlerThread;
    int bCount = 0;
    std::weak_ptr<GameplayLuaRuntime> weak = f.lua;
    f.bus.registerHandler(
        "test:relay", EnvelopeKind::Event,
        [weak, &bCount, &handlerThread]( const CommunicationEnvelope &env,
                                         std::vector<CommunicationEnvelope> &replies ) {
            bCount += 1;
            handlerThread = std::this_thread::get_id();
            if( auto rt = weak.lock() )
                rt->invoke( "b", "onRelay", "principal:b", env, replies );
        },
        "block:b", "core:test", std::nullopt, PayloadSchema::None );

    f.lua->loadScript( "a", R"(
        function onStart()
            bus.send( { kind="event", receiver="block:b", context="core:test",
                        action="test:relay", payload={schema="none"} } )
            return true
        end
    )" );
    f.lua->loadScript( "b", R"(
        function onRelay(msg)
            assert( msg.kind == "Event" )
            assert( msg.receiver == "block:b" )
            assert( msg.action == "test:relay" )
            assert( msg.sender == "principal:a" )
            return true
        end
    )" );
    const CommunicationEnvelope start = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "a", "onStart", "principal:a", start );
    f.bus.pumpAll();

    CHECK_EQ( bCount, 1 ); // exactly once, via submit -> A queue -> pump
    CHECK( handlerThread == std::this_thread::get_id() );
}

// ---------------------------------------------------------------------------
// H. Capability authorization: only registered host grants open the path
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_capability_routing_requires_host_grant )
{
    Fixture f;
    int guarded = 0;
    f.bus.registerHandler( "test:guarded", EnvelopeKind::Command,
                           [&guarded]( const CommunicationEnvelope &,
                                       std::vector<CommunicationEnvelope> & ) { guarded += 1; },
                           "world:guard", "core:test", std::optional<std::string>{
                                                          "core:guard" },
                           PayloadSchema::None );
    f.lua->loadScript( "guard", R"(
        function shoot()
            bus.send( { kind="command", receiver="world:guard", context="core:test",
                        action="test:guarded", capability="core:guard",
                        payload={schema="none"} } )
            return true
        end
    )" );
    const CommunicationEnvelope cause =
        makeEnvelope( 1, EnvelopeKind::Command, "player", "world:guard", "test:guarded" );

    // No grant for the sending principal -> the bus rejects loudly.
    bool rejected = false;
    try
    {
        (void)f.invoke( "guard", "shoot", "principal:h", cause );
    }
    catch( const GameplayLuaError & )
    {
        rejected = true;
    }
    CHECK( rejected );

    // Host-side grant turns the very same script path into a success.
    f.bus.pumpAll(); // nothing pending: the attempt never reached the bus
    f.bus.grantCapability( "principal:h", "core:guard" );
    (void)f.invoke( "guard", "shoot", "principal:h", cause );
    f.pumpAll();
    CHECK_EQ( guarded, 1 );
}

// ---------------------------------------------------------------------------
// I. Lua query -> C++ handler -> correlated reply back into Lua
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_lua_query_receives_cpp_reply )
{
    Fixture f;
    f.bus.registerHandler( "test:answer", EnvelopeKind::Query,
                           [& f ]( const CommunicationEnvelope &cause,
                                   std::vector<CommunicationEnvelope> &replies ) {
        CommandResultPayload result;
        result.ok = true;
        result.value = PropertyValue{ static_cast<std::uint32_t>( cause.messageId ) };
        replies.push_back( f.bus.makeReply( cause, std::move( result ) ) );
    },
                           "world:ans", "core:test", std::nullopt, PayloadSchema::Query );
    f.lua->loadScript( "asker", R"(
        function go()
            local answers = bus.query( { receiver="world:ans", context="core:test",
                                         action="test:answer",
                                         payload={schema="query", property="answer"} } )
            assert( #answers == 1, "expected exactly one reply" )
            local r = answers[1]
            assert( r.kind == "Reply" )
            assert( r.payload.schema == "command_result" )
            assert( r.payload.ok == true )
            assert( r.payload.value.value_type == "u32" )
            assert( tostring( r.payload.value.value ) == r.correlation_id, "correlation" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "asker", "go", "principal:p", cause ); // all asserts inside Lua
    CHECK( true );
}

// ---------------------------------------------------------------------------
// J. Lua query -> Lua handler -> bus.reply -> back to the querying script
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_lua_query_routes_through_lua_handler_reply )
{
    Fixture f;
    registerLuaHandler( f, "test:echo", EnvelopeKind::Query, "block:b",
                        PayloadSchema::Query, { "b", "onQuery", "principal:b" } );
    f.lua->loadScript( "b", R"(
        function onQuery(msg)
            bus.reply( { ok=true, value={ value_type="u32", value=tonumber( msg.message_id ) } } )
            return true
        end
    )" );
    f.lua->loadScript( "a", R"(
        local reply = { }
        function go()
            local answers = bus.query( { receiver="block:b", context="core:test",
                                         action="test:echo",
                                         payload={schema="query", property="x"} } )
            assert( #answers == 1, "expected one reply from the Lua handler" )
            local r = answers[1]
            assert( r.payload.schema == "command_result" )
            assert( r.payload.ok == true )
            assert( r.payload.value.value_type == "u32" )
            -- the handler echoed the query message id; correlation must match
            assert( tostring( r.payload.value.value ) == r.correlation_id, "correlation" )
            reply = r
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 77, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "a", "go", "principal:a", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// K. Nested invocation context: after B, A's cause/principal are restored
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_nested_query_restores_outer_context )
{
    Fixture f;
    registerLuaHandler( f, "test:nested.b", EnvelopeKind::Query, "block:b",
                        PayloadSchema::Query, { "b", "onQueryB", "principal:b" } );
    f.lua->loadScript( "b", R"(
        function onQueryB(msg)
            bus.reply( { ok=true, value={ value_type="u32", value=tonumber( msg.message_id ) } } )
            return true
        end
    )" );
    std::optional<std::uint64_t> observedCorrelation;
    f.bus.registerHandler( "test:nested.c", EnvelopeKind::Event,
                           [&observedCorrelation]( const CommunicationEnvelope &env,
                                                   std::vector<CommunicationEnvelope> & ) {
        observedCorrelation = env.correlationId;
    },
                           "block:c", "core:test", std::nullopt, PayloadSchema::None );
    f.lua->loadScript( "a", R"(
        function onA(msg)
            local answers = bus.query( { receiver="block:b", context="core:test",
                                         action="test:nested.b",
                                         payload={schema="query", property="x"} } )
            assert( #answers == 1 )
            -- after the nested invocation, A's own cause is still active
            bus.send( { kind="event", receiver="block:c", context="core:test",
                        action="test:nested.c", payload={schema="none"}, causal=true } )
            return true
        end
    )" );
    const CommunicationEnvelope cause =
        makeEnvelope( 777, EnvelopeKind::Command, "player", "a", "test:start" );
    const std::vector<CommunicationEnvelope> replies =
        f.invoke( "a", "onA", "principal:a", cause );
    f.pumpAll();
    CHECK_EQ( replies.size(), std::size_t{ 0 } ); // no context leak from B into A
    CHECK( observedCorrelation.has_value() );
    if( observedCorrelation )
        CHECK_EQ( *observedCorrelation, std::uint64_t{ 777 } ); // A's OWN cause
}

// ---------------------------------------------------------------------------
// L. Invocation depth guard: synchronous query recursion ends with a defined
//    error before the C++ stack can overflow
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_invocation_depth_guard )
{
    Fixture f;
    f.lua->setMaxInvocationDepth( 12 );
    f.bus.registerHandler( "test:recurse", EnvelopeKind::Query,
                           GameplayLuaRuntime::bridgeHandler( f.lua,
                                                               { "s", "recurse", "principal:s" } ),
                           "world:rec", "core:test", std::nullopt, PayloadSchema::Query );
    f.lua->loadScript( "s", R"(
        function recurse(msg)
            bus.query( { receiver="world:rec", context="core:test", action="test:recurse",
                         payload={schema="query", property="x"} } )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope(
        1, EnvelopeKind::Query, "player", "recurseTarget", "test:recurse",
        Payload{ QueryPayload{ "x" } } );
    bool caught = false;
    try
    {
        (void)f.invoke( "s", "recurse", "principal:s", cause );
    }
    catch( const GameplayLuaError & )
    {
        caught = true;
    }
    CHECK( caught );

    // The runtime remains usable.
    f.lua->loadScript( "ok", "function fine() return 1 end" );
    (void)f.invoke( "ok", "fine", "principal:s", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// M. bus.schedule_after_ms: plain envelope, id minted at scheduling,
//    delivered exactly once later with the SAME id
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_schedule_after_ms_transports_plain_envelope )
{
    std::vector<std::uint64_t> delivered;
    std::thread::id handlerThread;

    Fixture f;
    std::weak_ptr<GameplayLuaRuntime> weak = f.lua;
    f.bus.registerHandler(
        "test:sched", EnvelopeKind::Event,
        [weak, &delivered, &handlerThread]( const CommunicationEnvelope &env,
                                            std::vector<CommunicationEnvelope> &replies ) {
            handlerThread = std::this_thread::get_id();
            delivered.push_back( env.messageId );
            if( auto rt = weak.lock() )
                rt->invoke( "b", "onSched", "principal:b", env, replies );
        },
        "block:b", "core:test", std::nullopt, PayloadSchema::None );
    f.lua->loadScript( "a", R"(
        function onA()
            bus.schedule_after_ms( 5000,
                { kind="event", receiver="block:b", context="core:test",
                  action="test:sched", payload={schema="none"} } )
            return true
        end
    )" );
    f.lua->loadScript( "b", "function onSched(msg) assert( msg.action == 'test:sched' ) end" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "a", "onA", "principal:a", cause );

    // The message id was minted AT SCHEDULING: the next runtime id is
    // scheduledId + 1 (the scheduler transports the exact envelope).
    const std::uint64_t afterScheduling = f.bus.nextMessageId();
    const std::uint64_t scheduledId = afterScheduling - 1u;
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 1 } );

    // Nothing left the scheduler before the due time.
    f.clock.advance( std::chrono::milliseconds( 4999 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() == 0; } ) );
    (void)f.scheduler.drainDueTo( f.bus );
    f.pumpAll();
    CHECK( delivered.empty() );

    // Due -> worker/handoff -> owner drain -> runtime -> handler once.
    f.clock.advance( std::chrono::milliseconds( 1 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    (void)f.scheduler.drainDueTo( f.bus );
    f.pumpAll();
    CHECK_EQ( delivered.size(), std::size_t{ 1 } );
    if( !delivered.empty() )
    {
        CHECK_EQ( delivered[0], scheduledId ); // exact id, no re-issue at due time
        CHECK( handlerThread == std::this_thread::get_id() );
    }
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
}
// ---------------------------------------------------------------------------
// N. The scheduler worker never executes Lua: the target runs on the owner
//    thread, never on the worker
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_scheduler_worker_never_runs_lua )
{
    Fixture f;
    std::thread::id owner = std::this_thread::get_id();
    std::thread::id delivered;
    std::weak_ptr<GameplayLuaRuntime> weak = f.lua;
    f.bus.registerHandler(
        "test:owner_only", EnvelopeKind::Event,
        [weak, &delivered]( const CommunicationEnvelope &env,
                            std::vector<CommunicationEnvelope> &replies ) {
            delivered = std::this_thread::get_id();
            if( auto rt = weak.lock() )
                rt->invoke( "b", "onTicked", "principal:b", env, replies );
        },
        "block:b", "core:test", std::nullopt, PayloadSchema::None );
    f.lua->loadScript( "a", R"(
        function plan()
            bus.schedule_after_ms( 2000,
                { kind="event", receiver="block:b", context="core:test",
                  action="test:owner_only", payload={schema="none"} } )
            return true
        end
    )" );
    f.lua->loadScript( "b", R"(
        function onTicked(msg)
            assert( msg.action == "test:owner_only" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "a", "plan", "principal:a", cause );
    f.clock.advance( std::chrono::milliseconds( 2000 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    (void)f.scheduler.drainDueTo( f.bus );
    f.pumpAll();
    CHECK( delivered == owner ); // never the scheduler worker
    CHECK( f.scheduler.workerThreadId() != owner ); // a real worker exists
    CHECK( true );
}

// ---------------------------------------------------------------------------
// O. World binding is a read-only snapshot view (world.get_block only)
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_world_is_read_only_view )
{
    Fixture f;
    const BlockAddress pos = fromOriginOffset( 2, 3, 4 );
    f.chunks.setBlock( pos, f.stoneId );
    f.lua->loadScript( "worldview", R"(
        function probe()
            local here = world.get_block( { x=2, y=3, z=4 } )
            assert( here.loaded == true )
            assert( here.block_id == 1, "stone is runtime id 1" )
            local nowhere = world.get_block( { x=1000, y=1000, z=1000 } )
            assert( nowhere.loaded == false )
            assert( nowhere.block_id == nil )
            -- no mutating binding exists at all
            assert( world.set_block == nil )
            assert( world.remove_block == nil )
            assert( world.set_sidecar == nil )
            assert( world.get_sidecar == nil )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "worldview", "probe", "principal:p", cause );
    // The read view never mutated anything.
    const std::optional<std::uint16_t> after = f.state.blockAt( pos );
    CHECK( after.has_value() );
    if( after )
        CHECK_EQ( *after, f.stoneId );
}

// ---------------------------------------------------------------------------
// P. Lua error boundary: error() becomes a controlled GameplayLuaError that
//    names script+function, and the runtime stays usable
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_lua_error_boundary_is_controlled )
{
    Fixture f;
    f.lua->loadScript( "boom", R"(
        function explode()
            error( "kaboom" )
        end
        function calm()
            return 42
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "boom", "explode", "principal:p", cause );
    }
    catch( const GameplayLuaError &error )
    {
        caught = true;
        const std::string message = error.what();
        CHECK( message.find( "boom" ) != std::string::npos );    // script id
        CHECK( message.find( "explode" ) != std::string::npos ); // function name
        CHECK( message.find( "kaboom" ) != std::string::npos );  // lua error text
    }
    CHECK( caught );
    // Same runtime still runs perfectly valid handlers.
    (void)f.invoke( "boom", "calm", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// Q. Bus backpressure: bus.send never drops silently, never grows a retry
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_bus_send_backpressure_is_loud )
{
    Fixture f( 1, 8 ); // inbound capacity 1, outbound 8
    std::vector<std::uint64_t> seen;
    f.bus.registerHandler( "test:hold", EnvelopeKind::Event,
                           [&seen]( const CommunicationEnvelope &envelope,
                                    std::vector<CommunicationEnvelope> & ) {
        seen.push_back( envelope.messageId );
    },
                           "capture", "core:test", std::nullopt, PayloadSchema::None );
    f.lua->loadScript( "pressure", R"(
        function fill()
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:hold", payload={schema="none"} } )
            return true
        end
        function retry()
            local accepted = pcall( function()
                bus.send( { kind="event", receiver="capture", context="core:test",
                            action="test:hold", payload={schema="none"} } )
            end )
            return accepted
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );

    // First message fills the only inbound slot...
    (void)f.invoke( "pressure", "fill", "principal:p", cause );
    CHECK_EQ( f.bus.pendingInbound(), std::size_t{ 1 } );

    // ... so the second send must be LOUD (defined error, not a silent drop).
    bool loud = false;
    try
    {
        (void)f.invoke( "pressure", "fill", "principal:p", cause );
    }
    catch( const GameplayLuaError & )
    {
        loud = true;
    }
    CHECK( loud );

    // After the owner drains the bus, the same script path succeeds.
    f.pumpAll();
    CHECK_EQ( seen.size(), std::size_t{ 1 } );
    bool accepted = false;
    try
    {
        (void)f.invoke( "pressure", "retry", "principal:p", cause ); // pcall inside
        accepted = true;
    }
    catch( const GameplayLuaError & )
    {
        accepted = false;
    }
    CHECK( accepted );
    f.pumpAll();
    CHECK_EQ( seen.size(), std::size_t{ 2 } ); // both delivered, nothing dropped
}

// ---------------------------------------------------------------------------
// R. bus.reply outside any handler invocation is a defined rejection
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_reply_outside_handler_is_rejected )
{
    Fixture f;
    // Script top-level code runs with NO cause/handler context; the binding
    // must reject bus.reply there (loadScript itself surfaces the violation).
    f.lua->loadScript( "top", R"(
        local rejected = pcall( function()
            bus.reply( { ok=true } )
        end )
        assert( rejected == false, "bus.reply without a handler must be rejected" )
        function later() return 1 end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "top", "later", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// S. Payload codec roundtrip for every exposed schema + strict rejection of
//    invalid typed fields
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_payload_codec_roundtrips ) // S: every exposed schema, Lua -> C++
{
    Fixture f;

    // --- outbound: Lua-sent payloads arrive in C++ as the typed variant ---
    using Recorder = std::vector<Payload>;
    Recorder recorded;
    Recorder targetRecorded;
    Recorder propertyRecorded;
    f.bus.registerHandler( "test:val", EnvelopeKind::Event,
                           [&recorded]( const CommunicationEnvelope &env,
                                        std::vector<CommunicationEnvelope> & ) {
        recorded.push_back( env.payload );
    },
                           "capture", "core:test", std::nullopt, PayloadSchema::EventValue );
    f.bus.registerHandler( "test:target", EnvelopeKind::Event,
                           [&targetRecorded]( const CommunicationEnvelope &env,
                                              std::vector<CommunicationEnvelope> & ) {
        targetRecorded.push_back( env.payload );
    },
                           "capture", "core:test", std::nullopt, PayloadSchema::BlockTarget );
    f.bus.registerHandler( "test:property", EnvelopeKind::Event,
                           [&propertyRecorded]( const CommunicationEnvelope &env,
                                                std::vector<CommunicationEnvelope> & ) {
        propertyRecorded.push_back( env.payload );
    },
                           "capture", "core:test", std::nullopt, PayloadSchema::PropertySet );
    f.lua->loadScript( "codec", R"(
        function emit()
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:val",
                        payload={ schema="event_value", value={ value_type="u32", value=9 } } } )
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:val",
                        payload={ schema="event_value", value={ value_type="float", value=2.5 } } } )
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:target",
                        payload={ schema="block_target", target={ x=1, y=2, z=3 } } } )
            bus.send( { kind="event", receiver="capture", context="core:test",
                        action="test:property",
                        payload={ schema="property_set", property="test:value",
                                  value={ value_type="u32", value=11 } } } )
            return true
        end
        function bad()
            local badU32 = pcall( function()
                bus.send( { kind="event", receiver="capture", context="core:test",
                            action="test:val",
                            payload={ schema="event_value", value={ value_type="u32", value="NaN" } } } )
            end )
            assert( badU32 == false, "typed field violation must be loud" )
            local nonFinite = pcall( function()
                bus.send( { kind="event", receiver="capture", context="core:test",
                            action="test:val",
                            payload={ schema="event_value", value={ value_type="float", value=math.huge } } } )
            end )
            assert( nonFinite == false, "non-finite float must be rejected" )
            local floatOverflow = pcall( function()
                bus.send( { kind="event", receiver="capture", context="core:test",
                            action="test:val",
                            payload={ schema="event_value", value={ value_type="float", value=1e100 } } } )
            end )
            assert( floatOverflow == false, "float narrowing overflow must be rejected" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "player", "a", "x" );
    (void)f.invoke( "codec", "bad", "principal:p", cause ); // strict rejection inside
    (void)f.invoke( "codec", "emit", "principal:p", cause );
    f.pumpAll();

    CHECK_EQ( recorded.size(), std::size_t{ 2 } );
    bool payloadsOk = true;
    if( recorded.size() == 2u )
    {
        const auto &first = std::get<EventValuePayload>( recorded[0] );
        payloadsOk = std::holds_alternative<std::uint32_t>( first.value ) &&
                     std::get<std::uint32_t>( first.value ) == 9u;
        const auto &second = std::get<EventValuePayload>( recorded[1] );
        payloadsOk = payloadsOk &&
                     std::holds_alternative<float>( second.value ) &&
                     std::get<float>( second.value ) == 2.5f;
    }
    CHECK( payloadsOk );
    CHECK_EQ( targetRecorded.size(), std::size_t{ 1 } );
    if( targetRecorded.size() == 1u )
        CHECK( std::get<BlockTargetPayload>( targetRecorded[0] ).target ==
               fromOriginOffset( 1, 2, 3 ) );
    CHECK_EQ( propertyRecorded.size(), std::size_t{ 1 } );
    if( propertyRecorded.size() == 1u )
    {
        const auto &property = std::get<PropertySetPayload>( propertyRecorded[0] );
        CHECK_EQ( property.property, std::string{ "test:value" } );
        CHECK_EQ( std::get<std::uint32_t>( property.value ), std::uint32_t{ 11 } );
    }

    // --- in Lua: C++ -> Lua snapshot of the same payload ---
    f.lua->loadScript( "sniff", R"(
        function probe(msg)
            assert( msg.payload.schema == "event_value" )
            assert( msg.payload.value.value_type == "u32" )
            assert( msg.payload.value.value == 7 )
            return true
        end
    )" );
    const CommunicationEnvelope withEvent = makeEnvelope(
        5, EnvelopeKind::Event, "player", "capture", "test:val",
        Payload{ EventValuePayload{ PropertyValue{ std::uint32_t{ 7 } } } } );
    (void)f.invoke( "sniff", "probe", "principal:p", withEvent );

    f.lua->loadScript( "sniff_target", R"(
        function probe(msg)
            assert( msg.payload.schema == "block_target" )
            assert( msg.payload.target.x == -4 )
            assert( msg.payload.target.y == 5 )
            assert( msg.payload.target.z == 6 )
        end
        function property(msg)
            assert( msg.payload.schema == "property_set" )
            assert( msg.payload.property == "test:value" )
            assert( msg.payload.value.value_type == "u32" )
            assert( msg.payload.value.value == 12 )
        end
    )" );
    const CommunicationEnvelope withTarget = makeEnvelope(
        6, EnvelopeKind::Event, "player", "capture", "test:target",
        Payload{ BlockTargetPayload{ fromOriginOffset( -4, 5, 6 ) } } );
    (void)f.invoke( "sniff_target", "probe", "principal:p", withTarget );
    const CommunicationEnvelope withProperty = makeEnvelope(
        7, EnvelopeKind::Event, "player", "capture", "test:property",
        Payload{ PropertySetPayload{ "test:value", PropertyValue{ std::uint32_t{ 12 } } } } );
    (void)f.invoke( "sniff_target", "property", "principal:p", withProperty );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// T. Per-script namespace isolation: sabotaging script A can never poison
//    script B or a freshly loaded script C (table OBJECTS are per-script)
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_sandbox_namespaces_are_not_shared_between_scripts )
{
    Fixture f;
    f.lua->loadScript( "a", R"(
        function sabotage()
            bus.send = nil
            world.get_block = nil
            math.abs = nil
            table.insert = nil
            string.format = nil
            return true
        end
    )" );
    f.lua->loadScript( "b", R"(
        function verify()
            assert( type( bus.send ) == "function", "bus.send poisoned" )
            assert( type( world.get_block ) == "function", "world.get_block poisoned" )
            assert( type( math.abs ) == "function", "math.abs poisoned" )
            assert( type( table.insert ) == "function", "table.insert poisoned" )
            assert( type( string.format ) == "function", "string.format poisoned" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "a", "sabotage", "principal:a", cause );
    (void)f.invoke( "b", "verify", "principal:b", cause );

    // A freshly loaded script also gets an intact environment.
    f.lua->loadScript( "c", R"(
        function verify()
            assert( type( bus.send ) == "function" )
            assert( type( world.get_block ) == "function" )
            assert( type( math.abs ) == "function" )
            assert( type( table.insert ) == "function" )
            assert( type( string.format ) == "function" )
            return true
        end
    )" );
    (void)f.invoke( "c", "verify", "principal:c", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// U. Nested Lua queries can never reset the instruction budget (the count
//     hook is installed exactly once per state)
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_nested_queries_cannot_reset_budget )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    registerLuaHandler( f, "test:bounce", EnvelopeKind::Query, "block:b",
                        PayloadSchema::Query, { "b", "onQueryB", "principal:b" } );
    f.lua->loadScript( "b", "function onQueryB(msg) return true end" );
    f.lua->loadScript( "a", R"(
        function spin()
            for i = 1, 5000 do
                bus.query( { receiver="block:b", context="core:test",
                             action="test:bounce", payload={schema="query", property="x"} } )
            end
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "a", "spin", "principal:a", cause );
    }
    catch( const GameplayLuaError &error )
    {
        caught = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( caught ); // budget abort way before the 5000-query loop completed
    // Recovery: the same runtime still runs valid handlers.
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    f.lua->loadScript( "ok", "function fine() return 1 end" );
    (void)f.invoke( "ok", "fine", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// V/W. pcall/xpcall can never swallow an instruction-budget abort
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_pcall_cannot_swallow_budget_abort )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    f.lua->loadScript( "pv", R"(
        function spin()
            while true do
                pcall( function()
                    local x = 0
                    for i = 1, 10000 do x = x + i end
                end )
            end
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "pv", "spin", "principal:p", cause );
    }
    catch( const GameplayLuaError &error )
    {
        caught = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( caught );
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    f.lua->loadScript( "ok", "function fine() return 1 end" );
    (void)f.invoke( "ok", "fine", "principal:p", cause );
    CHECK( true );
}

TEST_CASE( m3r03_xpcall_cannot_swallow_budget_abort )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    f.lua->loadScript( "xv", R"(
        function spin()
            while true do
                xpcall( function()
                    local x = 0
                    for i = 1, 10000 do x = x + i end
                end, function(err) return err end )
            end
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "xv", "spin", "principal:p", cause );
    }
    catch( const GameplayLuaError &error )
    {
        caught = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( caught );
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    f.lua->loadScript( "ok", "function fine() return 1 end" );
    (void)f.invoke( "ok", "fine", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// X. rawset/rawget are NOT part of the sandbox - the read-only snapshot
//     proxy can therefore never be bypassed
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_rawset_snapshot_bypass_is_closed )
{
    Fixture f;
    f.lua->loadScript( "x", R"(
        function probe(msg)
            assert( rawset == nil, "rawset must not be in the sandbox" )
            assert( rawget == nil, "rawget must not be in the sandbox" )
            local changed = pcall( function() msg.sender = "evil" end )
            assert( changed == false, "snapshot writes must be rejected" )
            return true
        end
    )" );
    CommunicationEnvelope cause = makeEnvelope( 9, EnvelopeKind::Event, "player:1", "b", "test:x" );
    (void)f.invoke( "x", "probe", "principal:p", cause );
    CHECK_EQ( cause.sender, "player:1" ); // authoritative message untouched
}

// ---------------------------------------------------------------------------
// Y. Strict codec: boolean contract fields need real booleans; unknown
//     fields are rejected everywhere
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_strict_bool_and_unknown_field_codec )
{
    Fixture f;
    f.lua->loadScript( "y", R"(
        function rejectSpecs()
            local specs = {
                { { kind="event", receiver="r", context="core:t", action="test:y",
                    payload={schema="none"}, causal="false" }, "string causal" },
                { { kind="event", receiver="r", context="core:t", action="test:y",
                    payload={schema="none"}, causal=1 }, "numeric causal" },
                { { kind="event", receiver="r", context="core:t", action="test:y",
                    payload={ schema="event_value", unexpected=123,
                               value={ value_type="u32", value=1 } } }, "unknown payload field" },
                { { kind="event", receiver="r", context="core:t", action="test:y",
                    payload={ schema="none" }, extra_field=1 }, "unknown spec field" },
            }
            for _, entry in ipairs( specs ) do
                local ok = pcall( function() bus.send( entry[1] ) end )
                assert( ok == false, entry[2] .. " must be rejected" )
            end
            return true
        end
        function good()
            bus.send( { kind="event", receiver="rec", context="core:t",
                        action="test:cap", payload={schema="none"} } )
            return true
        end
    )" );
    f.bus.registerHandler( "test:cap", EnvelopeKind::Event,
                           []( const CommunicationEnvelope &, std::vector<CommunicationEnvelope> & ) {},
                           "rec", "core:t", std::nullopt, PayloadSchema::None );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "y", "rejectSpecs", "principal:p", cause ); // strict rejects
    (void)f.invoke( "y", "good", "principal:p", cause );        // valid payloads still pass
    f.pumpAll();

    // Strict reply codec through a real Lua handler (bus.reply needs one).
    registerLuaHandler( f, "test:yreply", EnvelopeKind::Command, "block:b",
                        PayloadSchema::None, { "b", "onYReply", "principal:b" } );
    f.lua->loadScript( "b", R"(
        function onYReply(msg)
            local ok = pcall( function() bus.reply( { ok="false" } ) end )
            assert( ok == false, "reply payload.ok must be a real boolean" )
            local ok2 = pcall( function() bus.reply( { ok=true, unexpected=123 } ) end )
            assert( ok2 == false, "unknown reply payload field must be rejected" )
            return true
        end
    )" );
    const CommunicationEnvelope replyRoute =
        makeEnvelope( 5, EnvelopeKind::Command, "p", "block:b", "test:yreply" );
    // Drive the reply-strictness checks through the Lua handler B itself.
    (void)f.invoke( "b", "onYReply", "principal:b", replyRoute );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// Z. Full uint64 message/correlation ids arrive as exact decimal strings
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_full_uint64_ids_are_exact_decimal_strings )
{
    Fixture f;
    f.lua->loadScript( "z", R"(
        function probe(msg)
            assert( msg.message_id == "18446744073709551615", "UINT64_MAX message_id" )
            assert( msg.correlation_id == "18446744073709551615", "UINT64_MAX correlation_id" )
            return true
        end
    )" );
    CommunicationEnvelope cause;
    cause.messageId = std::numeric_limits<std::uint64_t>::max();
    cause.kind = EnvelopeKind::Event;
    cause.sender = "player:1";
    cause.receiver = "b";
    cause.context = "core:test";
    cause.action = "test:u";
    cause.correlationId = std::numeric_limits<std::uint64_t>::max();
    (void)f.invoke( "z", "probe", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// AA (Lua side). schedule_after_ms with an unrepresentable delay is rejected
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_schedule_after_huge_delay_is_rejected )
{
    Fixture f;
    f.lua->loadScript( "huge", R"(
        function tryit()
            bus.schedule_after_ms( 9223372036854775807,
                { kind="event", receiver="b", context="core:t",
                  action="test:never", payload={schema="none"} } )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "huge", "tryit", "principal:p", cause );
    }
    catch( const GameplayLuaError & )
    {
        caught = true;
    }
    CHECK( caught );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
}

// ---------------------------------------------------------------------------
// AB. Duplicate script ids are rejected loudly (no hot reload in Round 3)
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_duplicate_script_id_is_rejected )
{
    Fixture f;
    f.lua->loadScript( "dup", "function one() return 1 end" );
    bool rejected = false;
    try
    {
        f.lua->loadScript( "dup", "function two() return 2 end" );
    }
    catch( const GameplayLuaError & )
    {
        rejected = true;
    }
    CHECK( rejected );
    // The ORIGINAL script still works.
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "dup", "one", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// AC. Every public GameplayLuaRuntime API is owner-thread enforced
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_foreign_thread_public_apis_are_rejected )
{
    Fixture f;
    f.lua->loadScript( "s", "function h(m) end" );
    std::atomic<bool> allRejected{ true };
    std::thread foreign( [&] {
        auto rejectApi = [&]( const std::function<void()> &call ) {
            bool rejected = false;
            try
            {
                call();
            }
            catch( const GameplayLuaError & )
            {
                rejected = true;
            }
            if( !rejected )
                allRejected.store( false );
        };
        rejectApi( [&] { (void)f.lua->hasScript( "s" ); } );
        rejectApi( [&] { (void)f.lua->hasFunction( "s", "h" ); } );
        rejectApi( [&] { f.lua->setInstructionBudget( 1'000'000 ); } );
        rejectApi( [&] { (void)f.lua->instructionBudget(); } );
        rejectApi( [&] { f.lua->setMaxInvocationDepth( 8 ); } );
        rejectApi( [&] { (void)f.lua->maxInvocationDepth(); } );
        rejectApi( [&] { f.lua->loadScript( "foreign", "x=1" ); } );
    } );
    foreign.join();
    CHECK( allRejected.load() );
    // The owner thread is unaffected.
    CHECK( f.lua->hasScript( "s" ) );
    CHECK( f.lua->hasFunction( "s", "h" ) );
    (void)f.invoke( "s", "h", "principal:p", makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" ) );
    CHECK( true );
}


// ---------------------------------------------------------------------------
// XA/XB/XC. xpcall has REAL Lua 5.4 semantics: results, arguments, normal
//     error-handler invocation - and the handler never leaks into results
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_xpcall_has_real_lua54_semantics )
{
    Fixture f;
    f.lua->loadScript( "x", R"(
        function probe()
            -- pcall keeps stock protected-call semantics, including an
            -- omitted/non-callable target and ordinary args/results.
            local pok0, perr0 = pcall()
            assert( pok0 == false and type( perr0 ) == "string",
                    "pcall() must protect the implicit nil call" )
            local pok1, psum, pextra = pcall(
                function(a, b) return a + b, "extra" end, 40, 2 )
            assert( pok1 == true and psum == 42 and pextra == "extra",
                    "pcall args/results" )

            -- XA: success without args
            local ok, value = xpcall(
                function() return 42 end,
                function(e) return e end )
            assert( ok == true, "XA ok" )
            assert( value == 42, "XA value" )

            -- XB: function arguments reach the function
            local ok2, sum = xpcall(
                function(a, b) return a + b end,
                function(e) return e end,
                41, 1 )
            assert( ok2 == true, "XB ok" )
            assert( sum == 42, "XB sum" )

            -- XC: a normal error runs the handler; handler result returned
            local ok3, handled = xpcall(
                function() error( "boom" ) end,
                function(e) return "handled:" .. tostring( e ) end )
            assert( ok3 == false, "XC ok" )
            assert( string.sub( handled, 1, 8 ) == "handled:", "XC handler result" )

            -- A non-callable first value is a protected call failure, not an
            -- argument-check error outside xpcall.
            local ok4, handled4 = xpcall( 42, function(e) return "not-callable" end )
            assert( ok4 == false, "XC non-callable status" )
            assert( handled4 == "not-callable", "XC non-callable handler" )

            -- A message handler contributes exactly one replacement error
            -- value; extra returns do not leak, and a failing handler remains
            -- an xpcall failure result rather than escaping the wrapper.
            local ok5, first, extra = xpcall(
                function() error( "many" ) end,
                function(e) return "first", "extra" end )
            assert( ok5 == false and first == "first" and extra == nil,
                    "XC exactly one handler result" )
            local handlerRuns = 0
            local ok6, handlerFailure = xpcall(
                function() error( "outer" ) end,
                function(e)
                    handlerRuns = handlerRuns + 1
                    error( "handler failed" )
                end )
            assert( ok6 == false, "XC handler failure stays protected" )
            assert( handlerFailure == "error in error handling",
                    "XC exact stock handler failure object" )
            assert( handlerRuns == 216,
                    "XC stock Lua 5.4 recursively reapplies a failing handler: " ..
                    tostring( handlerRuns ) )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "x", "probe", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// AD. Final release is owner-thread enforced before Lua teardown begins
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_foreign_thread_final_release_is_rejected )
{
    const pid_t child = fork();
    CHECK( child >= 0 );
    if( child == 0 )
    {
        // Make the expected std::terminate observable without producing a
        // core file. Construct after fork so no scheduler thread is inherited.
        std::set_terminate( [] { std::_Exit( 86 ); } );
        Fixture f;
        f.lua->loadScript( "lifetime", "function h() return true end" );
        std::shared_ptr<GameplayLuaRuntime> last = std::move( f.lua );
        std::thread foreign( [owned = std::move( last )]() mutable {
            owned.reset(); // must terminate before luaL_unref/lua_close
            std::_Exit( 87 );
        } );
        foreign.join();
        std::_Exit( 88 );
    }

    int status = 0;
    CHECK( waitpid( child, &status, 0 ) == child );
    CHECK( WIFEXITED( status ) );
    if( WIFEXITED( status ) )
        CHECK_EQ( WEXITSTATUS( status ), 86 );
}

// ---------------------------------------------------------------------------
// XD. A budget abort never invokes the script error handler and the private
//     sentinel can never be stored by Lua
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_xpcall_budget_never_invokes_error_handler )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    f.lua->loadScript( "xd", R"(
        captured = nil
        handlerRuns = 0
        function spin()
            xpcall(
                function()
                    while true do end
                end,
                function(e)
                    captured = e
                    handlerRuns = handlerRuns + 1
                    return e
                end )
            return true
        end
        function verifyCaptured()
            assert( captured == nil, "budget sentinel must never reach Lua" )
            assert( handlerRuns == 0, "error handler must not run on a budget abort" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "xd", "spin", "principal:p", cause );
    }
    catch( const GameplayLuaError &error )
    {
        caught = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( caught );
    // After the abort, a fresh invocation proves the handler never ran and
    // the sentinel was never storable.
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    (void)f.invoke( "xd", "verifyCaptured", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// XE. A budget abort produces NO bus/scheduler side effects - the error
//     handler (which would send/reply) never executes
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_xpcall_budget_error_handler_has_no_side_effects )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    f.lua->loadScript( "xe", R"(
        function spin()
            xpcall(
                function()
                    while true do end
                end,
                function(e)
                    bus.send( { kind="event", receiver="capture", context="core:t",
                                action="test:xe", payload={schema="none"} } )
                    return e
                end )
            return true
        end
    )" );
    f.bus.registerHandler( "test:xe", EnvelopeKind::Event,
                           []( const CommunicationEnvelope &, std::vector<CommunicationEnvelope> & ) {},
                           "capture", "core:t", std::nullopt, PayloadSchema::None );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    bool caught = false;
    try
    {
        (void)f.invoke( "xe", "spin", "principal:p", cause );
    }
    catch( const GameplayLuaError & )
    {
        caught = true;
    }
    CHECK( caught );
    // No message ever reached the runtime, no scheduler entry exists.
    CHECK_EQ( f.bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
    CHECK_EQ( f.scheduler.handoffCount(), std::size_t{ 0 } );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// XF. If an xpcall message handler itself exhausts the budget, Lua must not
//     recursively deliver the private abort sentinel to that handler. Normal
//     handler failures retain the exact stock recursion covered by XC above.
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_xpcall_handler_budget_abort_is_not_recursively_delivered )
{
    Fixture f;
    f.lua->setInstructionBudget( 20'000 );
    f.lua->loadScript( "xf", R"(
        handlerRuns = 0
        captured = nil
        function spinInHandler()
            xpcall(
                function() error( "ordinary failure" ) end,
                function(e)
                    handlerRuns = handlerRuns + 1
                    if handlerRuns == 2 then
                        -- A recursively delivered budget sentinel would be
                        -- observable here and could trigger every host-facing
                        -- side effect. None of this may run.
                        captured = e
                        bus.send( { kind="event", receiver="capture", context="core:t",
                                    action="test:xf", payload={schema="none"} } )
                        bus.schedule_after_ms( 1,
                            { kind="event", receiver="capture", context="core:t",
                              action="test:xf", payload={schema="none"} } )
                        bus.reply( { schema="none" } )
                    end
                    while true do end
                end )
            return true
        end
        function verifyRecovery()
            assert( handlerRuns == 1,
                    "budget abort recursively entered handler: " .. tostring( handlerRuns ) )
            assert( captured == nil, "private budget sentinel reached Lua" )
            return true
        end
    )" );
    f.bus.registerHandler( "test:xf", EnvelopeKind::Event,
                           []( const CommunicationEnvelope &, std::vector<CommunicationEnvelope> & ) {},
                           "capture", "core:t", std::nullopt, PayloadSchema::None );
    const CommunicationEnvelope cause =
        makeEnvelope( 1, EnvelopeKind::Query, "p", "a", "x" );
    std::vector<CommunicationEnvelope> replies;
    bool caughtBudget = false;
    try
    {
        f.lua->invoke( "xf", "spinInHandler", "principal:p", cause, replies );
    }
    catch( const GameplayLuaBudgetError &error )
    {
        caughtBudget = true;
        CHECK( std::string( error.what() ).find( "instruction budget exceeded" ) !=
               std::string::npos );
    }
    CHECK( caughtBudget );
    CHECK_EQ( replies.size(), std::size_t{ 0 } );
    CHECK_EQ( f.bus.pendingInbound(), std::size_t{ 0 } );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
    CHECK_EQ( f.scheduler.handoffCount(), std::size_t{ 0 } );

    // A fresh invocation proves exact one-shot handler execution, sentinel
    // privacy, and runtime recovery after the host receives the budget error.
    f.lua->setInstructionBudget( GameplayLuaRuntime::kDefaultInstructionBudget );
    (void)f.invoke( "xf", "verifyRecovery", "principal:p", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// MAJOR 3: the shared string-type metatable is unreachable (no
//     getmetatable in the sandbox), while setmetatable stays available
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_shared_string_metatable_is_unreachable )
{
    Fixture f;
    f.lua->loadScript( "a", R"(
        function sabotage()
            assert( getmetatable == nil, "getmetatable must not be in the sandbox" )
            return true
        end
    )" );
    f.lua->loadScript( "b", R"(
        function use()
            -- setmetatable stays: scripts build their own table patterns.
            local t = {}
            setmetatable( t, { __index = { foo = 42 } } )
            assert( t.foo == 42, "setmetatable/__index pattern must work" )
            assert( ( "abc" ):upper() == "ABC", "string library intact" )
            return true
        end
    )" );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "a", "sabotage", "principal:a", cause );
    (void)f.invoke( "b", "use", "principal:b", cause );
    CHECK( true );
}

// ---------------------------------------------------------------------------
// Plain-data wire codec: metatable-wrapped contract tables are rejected
// ---------------------------------------------------------------------------

TEST_CASE( m3r03_plain_data_wire_tables_reject_metatables )
{
    Fixture f;
    f.lua->loadScript( "pd", R"(
        function rejectAll()
            -- payload with a metatable-derived schema
            local payload = setmetatable( {}, { __index = { schema = "none" } } )
            local ok1 = pcall( function()
                bus.send( { kind="event", receiver="r", context="core:t",
                            action="test:pd", payload=payload } )
            end )
            assert( ok1 == false, "metatable payload must be rejected" )

            -- message spec with a metatable
            local spec = setmetatable( {},
                { __index = { kind="event", receiver="r", context="core:t",
                              action="test:pd", payload={schema="none"} } } )
            local ok2 = pcall( function() bus.send( spec ) end )
            assert( ok2 == false, "metatable spec must be rejected" )

            -- target with a metatable
            local target = setmetatable( {}, { __index = { x = 1, y = 2, z = 3 } } )
            local ok3 = pcall( function()
                bus.send( { kind="event", receiver="r", context="core:t",
                            action="test:pd", payload={schema="none"}, target=target } )
            end )
            assert( ok3 == false, "metatable target must be rejected" )
            return true
        end
        function good()
            bus.send( { kind="event", receiver="rec", context="core:t",
                        action="test:pdcap", payload={schema="none"} } )
            return true
        end
    )" );
    f.bus.registerHandler( "test:pdcap", EnvelopeKind::Event,
                           []( const CommunicationEnvelope &, std::vector<CommunicationEnvelope> & ) {},
                           "rec", "core:t", std::nullopt, PayloadSchema::None );
    const CommunicationEnvelope cause = makeEnvelope( 1, EnvelopeKind::Command, "p", "a", "x" );
    (void)f.invoke( "pd", "rejectAll", "principal:p", cause );
    (void)f.invoke( "pd", "good", "principal:p", cause ); // plain tables stay valid
    f.pumpAll();

    // Reply payload + PropertyValue with metatables (inside a real handler).
    registerLuaHandler( f, "test:pdreply", EnvelopeKind::Command, "block:b",
                        PayloadSchema::None, { "b", "onPdReply", "principal:b" } );
    f.lua->loadScript( "b", R"(
        function onPdReply(msg)
            local ok1 = pcall( function()
                bus.reply( setmetatable( {}, { __index = { ok = true } } ) )
            end )
            assert( ok1 == false, "metatable reply payload must be rejected" )
            local ok2 = pcall( function()
                bus.reply( { ok=true, value=setmetatable( {},
                            { __index = { value_type="u32", value=1 } } ) } )
            end )
            assert( ok2 == false, "metatable PropertyValue must be rejected" )
            return true
        end
    )" );
    const CommunicationEnvelope replyRoute =
        makeEnvelope( 5, EnvelopeKind::Command, "p", "block:b", "test:pdreply" );
    (void)f.invoke( "b", "onPdReply", "principal:b", replyRoute );
    CHECK( true );
}

int main() { return test::runAll(); }
