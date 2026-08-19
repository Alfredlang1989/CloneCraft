#include "TestHarness.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/communication/SchedulerClock.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace world;
    using namespace world::communication;

    using Time = SchedulerClock::Time;

    /**
     * Deterministic fake clock with the SAME pending-safe interrupt contract
     * as SteadySchedulerClock: an interrupt arriving immediately before
     * waitUntil() aborts that wait. Instant advance; plus test-only
     * active-wait introspection (activelyWaiting / activeWaitTarget /
     * isActivelyWaitingOn) so tests can PROVE the worker is genuinely
     * blocking inside waitUntil() on a specific target before scheduling an
     * earlier timer - no timing luck. The latch is set only AFTER a pending
     * interrupt has been consumed and the blocking wait is actually entered,
     * so "active" really means "currently blocked".
     */
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
            // Pending-safe: consume an interrupt that arrived before this
            // entry - the following wait is aborted immediately and is NOT
            // an active (blocking) wait.
            if( mPendingInterrupt )
            {
                mPendingInterrupt = false;
                return;
            }
            // Only NOW is the calling thread genuinely blocking inside this
            // waitUntil(): publish the active-wait latch so tests can PROVE
            // the worker is really blocked on this target (no pending
            // interrupt consumed, wait actually entered).
            mActivelyWaiting = true;
            mActiveWaitTarget = until;
            while( mNow < until && !mPendingInterrupt )
                mCv.wait( lock );
            mPendingInterrupt = false;
            mActivelyWaiting = false;
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

        /** Test-only: jump the virtual clock to an absolute time (used to
         *  exercise the absolute due-time overflow guard). */
        void setNow( Time target )
        {
            {
                std::lock_guard<std::mutex> lock( mMutex );
                mNow = target;
            }
            mCv.notify_all();
        }

        // --- test-only introspection (read-only, thread-safe) ---
        /** True only while a thread is genuinely blocked inside waitUntil()
         *  (after a pending interrupt has been consumed and the blocking
         *  wait was actually entered). */
        bool activelyWaiting() const
        {
            std::lock_guard<std::mutex> lock( mMutex );
            return mActivelyWaiting;
        }
        Time activeWaitTarget() const
        {
            std::lock_guard<std::mutex> lock( mMutex );
            return mActiveWaitTarget;
        }
        bool isActivelyWaitingOn( Time target ) const
        {
            std::lock_guard<std::mutex> lock( mMutex );
            return mActivelyWaiting && mActiveWaitTarget == target;
        }

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;
        Time mNow{};
        bool mPendingInterrupt = false;
        bool mActivelyWaiting = false;
        Time mActiveWaitTarget{};
    };

    bool waitFor( const std::function<bool()> &condition, int timeoutMs = 500 )
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

    CommunicationEnvelope markEnvelope( std::uint64_t id )
    {
        CommunicationEnvelope env;
        env.messageId = id;
        env.kind = EnvelopeKind::Command;
        env.sender = "player:1";
        env.receiver = "world:state";
        env.context = "core:world";
        env.action = "test:mark";
        return env;
    }

    struct HandlerObserved
    {
        std::uint64_t messageId = 0;
        std::string sender;
        std::string action;
    };

    struct Fixture
    {
        TestSchedulerClock clock;
        DelayedMessageScheduler scheduler{ clock, 16 };
        CommunicationRuntime runtime{ 32, 32 };
        std::vector<HandlerObserved> fired;
        std::thread::id handlerThread{};

        Fixture()
        {
            runtime.registerHandler( "test:mark", EnvelopeKind::Command,
                                     [this]( const CommunicationEnvelope &env,
                                             std::vector<CommunicationEnvelope> & ) {
                handlerThread = std::this_thread::get_id();
                fired.push_back( HandlerObserved{ env.messageId, env.sender, env.action } );
            },
                std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
                PayloadSchema::None );
        }
        ~Fixture() { scheduler.shutdown(); }
    };

    /** drain due handoff into the runtime's inbound A, then execute the
     *  inbound queue completely (owner/game thread semantics). */
    void drainAndPump( DelayedMessageScheduler &scheduler, CommunicationRuntime &runtime )
    {
        (void)scheduler.drainDueTo( runtime );
        while( runtime.pendingInbound() > 0u )
            (void)runtime.pumpOne();
    }
} // namespace

// ---------------------------------------------------------------------
// A. Not-due stays waiting (no submit, no handler, until clock reaches it)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_not_due_stays_out_of_the_bus )
{
    Fixture f;
    const Time due = f.clock.now() + std::chrono::seconds( 10 );
    f.scheduler.scheduleAt( due, markEnvelope( 1001 ) );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 1 } );

    // Clock at T: nothing may reach the bus.
    CHECK_EQ( f.scheduler.drainDueTo( f.runtime ), std::size_t{ 0 } );
    drainAndPump( f.scheduler, f.runtime );
    CHECK( f.fired.empty() );
    CHECK_EQ( f.scheduler.handoffCount(), std::size_t{ 0 } );

    // T+9: still not due (strict: due <= now is false).
    f.clock.advance( std::chrono::seconds( 9 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() == 0u; } ) );
    CHECK_EQ( f.scheduler.drainDueTo( f.runtime ), std::size_t{ 0 } );
    CHECK( f.fired.empty() );

    // T+10: due -> handoff -> owner drain -> exact delivery.
    f.clock.advance( std::chrono::seconds( 1 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    CHECK( f.scheduler.drainDueTo( f.runtime ) >= std::size_t{ 1 } );
    drainAndPump( f.scheduler, f.runtime );
    CHECK_EQ( f.fired.size(), std::size_t{ 1 } );
    if( !f.fired.empty() )
        CHECK_EQ( f.fired[0].messageId, std::uint64_t{ 1001 } );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
}

// ---------------------------------------------------------------------
// B. Deterministic equal due-time order via sequence (repeatable)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_equal_due_time_is_deterministic_by_sequence )
{
    for( int run = 0; run < 2; ++run )
    {
        Fixture f;
        const Time runDue = f.clock.now() + std::chrono::seconds( 10 );
        f.scheduler.scheduleAt( runDue, markEnvelope( 1001 ) );
        f.scheduler.scheduleAt( runDue, markEnvelope( 1002 ) );
        f.scheduler.scheduleAt( runDue, markEnvelope( 1003 ) );
        f.clock.advance( std::chrono::seconds( 10 ) );
        CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 3u; } ) );
        drainAndPump( f.scheduler, f.runtime );
        CHECK_EQ( f.fired.size(), std::size_t{ 3 } );
        if( f.fired.size() == 3u )
        {
            CHECK_EQ( f.fired[0].messageId, std::uint64_t{ 1001 } ); // sequence order
            CHECK_EQ( f.fired[1].messageId, std::uint64_t{ 1002 } );
            CHECK_EQ( f.fired[2].messageId, std::uint64_t{ 1003 } );
        }
    }
}

// ---------------------------------------------------------------------
// C. Different due times fire in due order
// ---------------------------------------------------------------------

TEST_CASE( m03r2_different_due_times_fire_in_order )
{
    Fixture f;
    const Time base = f.clock.now();
    f.scheduler.scheduleAt( base + std::chrono::seconds( 30 ), markEnvelope( 1003 ) );
    f.scheduler.scheduleAt( base + std::chrono::seconds( 10 ), markEnvelope( 1001 ) );
    f.scheduler.scheduleAt( base + std::chrono::seconds( 20 ), markEnvelope( 1002 ) );

    f.clock.advance( std::chrono::seconds( 10 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );
    f.clock.advance( std::chrono::seconds( 10 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );
    f.clock.advance( std::chrono::seconds( 10 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );

    CHECK_EQ( f.fired.size(), std::size_t{ 3 } );
    if( f.fired.size() == 3u )
    {
        CHECK_EQ( f.fired[0].messageId, std::uint64_t{ 1001 } );
        CHECK_EQ( f.fired[1].messageId, std::uint64_t{ 1002 } );
        CHECK_EQ( f.fired[2].messageId, std::uint64_t{ 1003 } );
    }
}

// ---------------------------------------------------------------------
// D. The worker thread never executes handlers / touches the runtime
// ---------------------------------------------------------------------

TEST_CASE( m03r2_worker_never_executes_bus_work )
{
    Fixture f;
    const std::thread::id mainThread = std::this_thread::get_id();
    CHECK( f.scheduler.workerThreadId() != mainThread ); // real worker thread exists

    f.scheduler.scheduleAt( f.clock.now() + std::chrono::milliseconds( 10 ),
                            markEnvelope( 2001 ) );
    f.clock.advance( std::chrono::milliseconds( 10 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );

    // The handler - the only bus side effect - runs on the OWNER thread
    // (drain + pump), never on the worker: the scheduler has no runtime
    // reachable from its worker (structural: only drainDueTo(runtime) on
    // the owner exposes the runtime to the scheduler).
    CHECK_EQ( f.fired.size(), std::size_t{ 1 } );
    CHECK( f.handlerThread == mainThread );
}

// ---------------------------------------------------------------------
// E. End-to-end: schedule -> worker/handoff -> owner drain -> runtime
// ---------------------------------------------------------------------

TEST_CASE( m03r2_end_to_end_schedules_and_delivers_unchanged )
{
    Fixture f;
    f.scheduler.scheduleAfter( std::chrono::seconds( 5 ), markEnvelope( 777 ) );
    f.clock.advance( std::chrono::seconds( 5 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );

    CHECK_EQ( f.scheduler.handoffCount(), std::size_t{ 1 } );
    CHECK( f.scheduler.drainDueTo( f.runtime ) >= std::size_t{ 1 } );
    drainAndPump( f.scheduler, f.runtime );

    CHECK_EQ( f.fired.size(), std::size_t{ 1 } );
    if( !f.fired.empty() )
    {
        // Envelope data travels untouched (messageId/sender/action as scheduled).
        CHECK_EQ( f.fired[0].messageId, std::uint64_t{ 777 } );
        CHECK_EQ( f.fired[0].sender, "player:1" );
        CHECK_EQ( f.fired[0].action, "test:mark" );
    }
}

// ---------------------------------------------------------------------
// F. Backpressure: the head-of-line front stays in the bounded handoff
// ---------------------------------------------------------------------

TEST_CASE( m03r2_backpressure_keeps_head_of_line_in_bounded_handoff )
{
    CommunicationRuntime small{ 1, 8 }; // inbound capacity 1
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler{ clock, 16 };
    std::vector<std::uint64_t> fired;
    small.registerHandler( "test:mark", EnvelopeKind::Command,
                           [&fired]( const CommunicationEnvelope &env,
                                     std::vector<CommunicationEnvelope> & ) {
        fired.push_back( env.messageId );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const Time due = clock.now() + std::chrono::seconds( 1 );
    scheduler.scheduleAt( due, markEnvelope( 2001 ) );
    scheduler.scheduleAt( due, markEnvelope( 2002 ) );
    clock.advance( std::chrono::seconds( 1 ) );
    CHECK( waitFor( [&] { return scheduler.handoffCount() >= 2u; } ) );

    // First drain: 2001 enters the (full-now) inbound A, 2002 meets a full
    // queue and must REMAIN as the handoff front - not lost, not re-idded,
    // not overtaken, no unbounded retry queue.
    (void)scheduler.drainDueTo( small );
    CHECK_EQ( small.pendingInbound(), std::size_t{ 1 } );
    CHECK_EQ( scheduler.handoffCount(), std::size_t{ 1 } ); // bounded head-of-line
    CHECK( fired.empty() );                                 // nothing ran yet

    // Owner pumps the bus, then the retry succeeds with the SAME envelope.
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne();
    CHECK( scheduler.drainDueTo( small ) >= std::size_t{ 1 } );
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne();

    CHECK_EQ( fired.size(), std::size_t{ 2 } );
    if( fired.size() == 2u )
    {
        CHECK_EQ( fired[0], std::uint64_t{ 2001 } );
        CHECK_EQ( fired[1], std::uint64_t{ 2002 } ); // same id - no re-issue
    }
    CHECK_EQ( scheduler.handoffCount(), std::size_t{ 0 } );

    scheduler.shutdown();
}

// ---------------------------------------------------------------------
// G. Several due messages vs. bounded runtime A: no loss, no reorder
// ---------------------------------------------------------------------

TEST_CASE( m03r2_three_due_messages_small_inbound_no_loss_no_reorder )
{
    CommunicationRuntime small{ 1, 8 };
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler{ clock, 16 };
    std::vector<std::uint64_t> fired;
    small.registerHandler( "test:mark", EnvelopeKind::Command,
                           [&fired]( const CommunicationEnvelope &env,
                                     std::vector<CommunicationEnvelope> & ) {
        fired.push_back( env.messageId );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );

    const Time due = clock.now() + std::chrono::seconds( 2 );
    scheduler.scheduleAt( due, markEnvelope( 3001 ) );
    scheduler.scheduleAt( due, markEnvelope( 3002 ) );
    scheduler.scheduleAt( due, markEnvelope( 3003 ) );
    clock.advance( std::chrono::seconds( 2 ) );
    CHECK( waitFor( [&] { return scheduler.handoffCount() >= 2u; } ) ); // bounded stage

    // Step 1: only 3001 fits into the capacity-1 inbound queue; the drain
    // stops head-of-line with 3002, 3003 still staged.
    (void)scheduler.drainDueTo( small );
    CHECK_EQ( scheduler.handoffCount(), std::size_t{ 2 } );
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne();

    // Step 2: 3002.
    (void)scheduler.drainDueTo( small );
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne();

    // Step 3: 3003.
    (void)scheduler.drainDueTo( small );
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne();

    CHECK_EQ( fired.size(), std::size_t{ 3 } );
    if( fired.size() == 3u )
    {
        CHECK_EQ( fired[0], std::uint64_t{ 3001 } );
        CHECK_EQ( fired[1], std::uint64_t{ 3002 } );
        CHECK_EQ( fired[2], std::uint64_t{ 3003 } );
    }
    CHECK_EQ( scheduler.handoffCount(), std::size_t{ 0 } );

    scheduler.shutdown();
}

// ---------------------------------------------------------------------
// H. An earlier newly scheduled entry wakes the worker (deterministic)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_earlier_schedule_wakes_the_worker )
{
    Fixture f;
    const auto wallStart = std::chrono::steady_clock::now();

    // 1. Worker starts waiting on T+100; PROVE it is genuinely blocked
    //    inside waitUntil(T+100) (isActivelyWaitingOn is only true after a
    //    pending interrupt was consumed and the blocking wait was entered).
    const Time t100 = f.clock.now() + std::chrono::seconds( 100 );
    f.scheduler.scheduleAt( t100, markEnvelope( 9001 ) );
    CHECK( waitFor( [&] { return f.clock.isActivelyWaitingOn( t100 ); } ) );

    // 2. ONLY NOW schedule the earlier T+10 while the T+100 wait is truly
    //    active (clock still at T).
    const Time t10 = f.clock.now() + std::chrono::seconds( 10 );
    f.scheduler.scheduleAt( t10, markEnvelope( 9002 ) );

    // 3./4. The T+10 schedule must abort the ACTIVE T+100 wait; advancing
    //       only to T+10 makes T+10 due while T+100 stays queued.
    f.clock.advance( std::chrono::seconds( 10 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );
    CHECK_EQ( f.fired.size(), std::size_t{ 1 } );
    if( !f.fired.empty() )
        CHECK_EQ( f.fired[0].messageId, std::uint64_t{ 9002 } );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 1 } ); // T+100 still queued

    // 5. T+100 fires later, normally.
    f.clock.advance( std::chrono::seconds( 90 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() >= 1u; } ) );
    drainAndPump( f.scheduler, f.runtime );
    CHECK_EQ( f.fired.size(), std::size_t{ 2 } );
    if( f.fired.size() >= 2u )
        CHECK_EQ( f.fired[1].messageId, std::uint64_t{ 9001 } );

    // The worker did not sleep until the 100s target in real time.
    const auto wallElapsed = std::chrono::steady_clock::now() - wallStart;
    CHECK( wallElapsed < std::chrono::seconds( 2 ) );
}

// ---------------------------------------------------------------------
// H2. Pending interrupt immediately before waitUntil() is not lost
// ---------------------------------------------------------------------

TEST_CASE( m03r2_pending_interrupt_before_wait_is_not_lost )
{
    // Direct clock contract (TestClock): the interrupt is armed BEFORE the
    // wait starts; waitUntil() must return immediately, not wait for the old
    // far-away target.
    TestSchedulerClock clock;
    const Time far = clock.now() + std::chrono::hours( 1 );
    clock.interrupt();
    const auto start = std::chrono::steady_clock::now();
    clock.waitUntil( far ); // must be aborted by the pending interrupt
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK( elapsed < std::chrono::milliseconds( 200 ) );

    // Same contract for the production clock.
    SteadySchedulerClock steady;
    steady.interrupt();
    const Time steadyFar = steady.now() + std::chrono::hours( 1 );
    const auto start2 = std::chrono::steady_clock::now();
    steady.waitUntil( steadyFar );
    const auto elapsed2 = std::chrono::steady_clock::now() - start2;
    CHECK( elapsed2 < std::chrono::milliseconds( 200 ) );
}

// ---------------------------------------------------------------------
// I. Clean shutdown: no hang, no detached zombie
// ---------------------------------------------------------------------

TEST_CASE( m03r2_shutdown_is_clean_and_idempotent )
{
    {
        Fixture f;
        f.scheduler.scheduleAt( f.clock.now() + std::chrono::seconds( 60 ),
                                markEnvelope( 4001 ) );
        f.clock.advance( std::chrono::seconds( 30 ) ); // worker waits
        CHECK( f.scheduler.running() );
        f.scheduler.shutdown();                        // joins the waiting worker
        CHECK( !f.scheduler.running() );
        f.scheduler.shutdown();                        // idempotent - no hang
    }                                                  // destructor again - no zombie
    CHECK( true );
}

// ---------------------------------------------------------------------
// J. Sequence overflow is a defined, loud error (never silent wrap)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_sequence_overflow_is_loud )
{
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler{ clock, 8,
                                       std::numeric_limits<std::uint64_t>::max() - 2 };
    scheduler.scheduleAt( clock.now(), markEnvelope( 5001 ) );
    scheduler.scheduleAt( clock.now(), markEnvelope( 5002 ) );
    bool overflowDetected = false;
    try
    {
        scheduler.scheduleAt( clock.now(), markEnvelope( 5003 ) );
    }
    catch( const world::communication::CommunicationError & )
    {
        overflowDetected = true;
    }
    CHECK( overflowDetected ); // loud and deterministic, never wrapped to 0
    scheduler.shutdown();
}

// ---------------------------------------------------------------------
// K. Bounded head-of-line backpressure under permanent temporary load
// ---------------------------------------------------------------------

TEST_CASE( m03r2_repeated_drain_stays_bounded_and_eventually_delivers_all )
{
    // handoffCapacity = 2; runtime A already full; 100 immediately due
    // messages. Repeated drainDueTo() WITHOUT pumping A must never grow an
    // unbounded retry structure: the due staging stays <= 2 and the
    // scheduled queue keeps the rest.
    CommunicationRuntime small{ 1, 8 };
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler{ clock, 2 }; // bounded head-of-line stage
    std::vector<std::uint64_t> fired;
    small.registerHandler( "test:mark", EnvelopeKind::Command,
                           [&fired]( const CommunicationEnvelope &env,
                                     std::vector<CommunicationEnvelope> & ) {
        fired.push_back( env.messageId );
    },
        std::string{ "world:state" }, std::string{ "core:world" }, std::nullopt,
        PayloadSchema::None );
    // Fill runtime A so the very first drain hits backpressure.
    small.registerHandler( "test:hold", EnvelopeKind::Command,
                           []( const CommunicationEnvelope &,
                               std::vector<CommunicationEnvelope> & ) {},
                           std::string{ "world:state" }, std::string{ "core:world" },
                           std::nullopt, PayloadSchema::None );
    auto holder = markEnvelope( 1u );
    holder.action = "test:hold";
    CHECK( small.submit( holder ) ); // A now full (capacity 1)

    constexpr int N = 100;
    for( int i = 0; i < N; ++i )
        scheduler.scheduleAt( clock.now(), markEnvelope( 4000u + static_cast<std::uint64_t>( i + 1 ) ) );
    clock.advance( std::chrono::milliseconds( 1 ) );
    CHECK( waitFor( [&] { return scheduler.handoffCount() == 2u; } ) ); // bounded fill

    // Repeated drains without pumping the bus: bounded, no loss, no growth.
    for( int i = 0; i < 10; ++i )
    {
        CHECK_EQ( scheduler.drainDueTo( small ), std::size_t{ 0 } ); // head-of-line stops
        CHECK( scheduler.handoffCount() <= 2u );                     // bounded
    }
    CHECK( fired.empty() );

    // Now pump the bus step by step: everything is delivered exactly once,
    // in original order, with the original message ids. The worker refills
    // the handoff asynchronously, so each step waits for the next staged
    // envelope (bounded wait, no timing luck).
    while( small.pendingInbound() > 0u )
        (void)small.pumpOne(); // releases the holder slot
    while( scheduler.scheduledCount() > 0u || scheduler.handoffCount() > 0u )
    {
        CHECK( waitFor( [&] { return scheduler.handoffCount() > 0u; }, 2000 ) );
        (void)scheduler.drainDueTo( small ); // head-of-line: front into A
        while( small.pendingInbound() > 0u )
            (void)small.pumpOne();
    }

    CHECK_EQ( fired.size(), std::size_t{ N } );
    bool orderedAndUnique = fired.size() == static_cast<std::size_t>( N );
    for( int i = 0; i < N && orderedAndUnique; ++i )
    {
        orderedAndUnique = fired[static_cast<std::size_t>( i )] ==
                           ( 4000u + static_cast<std::uint64_t>( i + 1 ) );
    }
    CHECK( orderedAndUnique ); // original order + original ids, exactly once
    CHECK_EQ( scheduler.scheduledCount(), std::size_t{ 0 } );
    CHECK_EQ( scheduler.handoffCount(), std::size_t{ 0 } );

    scheduler.shutdown();
}

// ---------------------------------------------------------------------
// L. scheduleAt/scheduleAfter after shutdown are rejected loudly
// ---------------------------------------------------------------------

TEST_CASE( m03r2_schedule_after_shutdown_is_rejected )
{
    Fixture f;
    f.scheduler.shutdown();

    bool rejected = false;
    try
    {
        f.scheduler.scheduleAt( f.clock.now() + std::chrono::seconds( 1 ),
                                markEnvelope( 6001 ) );
    }
    catch( const world::communication::CommunicationError & )
    {
        rejected = true;
    }
    CHECK( rejected ); // loud, defined - not silent loss

    bool rejectedAfter = false;
    try
    {
        f.scheduler.scheduleAfter( std::chrono::seconds( 1 ), markEnvelope( 6002 ) );
    }
    catch( const world::communication::CommunicationError & )
    {
        rejectedAfter = true;
    }
    CHECK( rejectedAfter );

    // No queue mutation, no sequence consumed.
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
    CHECK_EQ( f.scheduler.handoffCount(), std::size_t{ 0 } );
}

// ---------------------------------------------------------------------
// M. handoffCapacity == 0 is rejected by the constructor (no worker)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_zero_handoff_capacity_is_rejected )
{
    TestSchedulerClock clock;
    bool rejected = false;
    try
    {
        DelayedMessageScheduler scheduler{ clock, 0 };
    }
    catch( const world::communication::CommunicationError & )
    {
        rejected = true;
    }
    CHECK( rejected ); // CommunicationError per module convention
}

// ---------------------------------------------------------------------
// N. Parallel introspection vs shutdown: no unsynchronized std::thread
//    access (MAJOR regression - running()/workerThreadId() must read
//    scheduler-owned state, never the live std::thread object)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_parallel_introspection_with_shutdown_is_safe )
{
    Fixture f;
    f.scheduler.scheduleAt( f.clock.now() + std::chrono::seconds( 60 ),
                            markEnvelope( 7001 ) );
    f.clock.advance( std::chrono::seconds( 30 ) ); // worker waits

    // Thread A: hammer the introspection read paths.
    std::atomic<bool> done{ false };
    std::thread introspector( [&] {
        while( !done.load( std::memory_order_relaxed ) )
        {
            (void)f.scheduler.running();
            (void)f.scheduler.workerThreadId();
        }
    } );

    // Thread B: shutdown() sets mStopping under the mutex, then joins the
    // worker OUTSIDE the mutex. Introspection never touches the std::thread
    // object, so this join can never race a running()/workerThreadId() read.
    f.scheduler.shutdown();
    done.store( true, std::memory_order_relaxed );
    introspector.join(); // proves shutdown() itself did not hang either

    CHECK( !f.scheduler.running() ); // worker joined: cleanly stopped
}

// ---------------------------------------------------------------------
// O. Absolute due-time overflow: a positive delay beyond the remaining
//    representable range is rejected loudly (approved Round-2 fix)
// ---------------------------------------------------------------------

TEST_CASE( m03r2_schedule_after_absolute_overflow_is_rejected )
{
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler{ clock, 8 };

    // Jump the virtual clock almost to the end of the representable range:
    // only 1 second remains.
    const Time maxTime = Time::max();
    const Time nearlyMax = maxTime - std::chrono::seconds( 1 );
    clock.setNow( nearlyMax );

    // A surviving delay still works at the edge.
    scheduler.scheduleAfter( std::chrono::seconds( 1 ), markEnvelope( 8001 ) );
    CHECK_EQ( scheduler.scheduledCount(), std::size_t{ 1 } );
    scheduler.shutdown();

    // A delay beyond the remaining range must NOT wrap the clock.
    TestSchedulerClock clock2;
    DelayedMessageScheduler scheduler2{ clock2, 8 };
    clock2.setNow( maxTime - std::chrono::seconds( 1 ) );
    bool rejected = false;
    try
    {
        scheduler2.scheduleAfter( std::chrono::seconds( 2 ), markEnvelope( 8002 ) );
    }
    catch( const world::communication::CommunicationError & )
    {
        rejected = true;
    }
    CHECK( rejected ); // CommunicationError, no wrap, no UB
    CHECK_EQ( scheduler2.scheduledCount(), std::size_t{ 0 } );
    scheduler2.shutdown();
}

int main() { return test::runAll(); }
