#pragma once

#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationError.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/communication/SchedulerClock.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace world::communication
{
    /**
     * M03 Round 2: delayed-message scheduler (Time Worker).
     *
     * A timer is ONLY "put this fully transportable CommunicationEnvelope
     * back into the normal communication path at time X". The stored unit
     * is exactly `due_time + deterministic sequence + CommunicationEnvelope`
     * - no std::function, no callback, no Lua ref, no WorldState/registry/
     * renderer reference. The scheduler is NOT a router and reinterprets no
     * message field (messageId, sender, receiver, context, action, target,
     * correlation stay untouched).
     *
     * Hard thread boundary (Round-1 A/B queues are owner-thread):
     *
     *   producer (any thread) --scheduleAt/After--> scheduler
     *        priority queue (due_time, sequence)
     *        worker thread waits (interruptible, no busy wait)
     *        due entries -> thread-safe due handoff
     *        owner/game thread --drainDueTo(runtime)--> runtime.submit()
     *        -> existing inbound A queue -> normal dispatcher
     *
     * The worker thread NEVER touches CommunicationRuntime, WorldState,
     * Lua or the renderer; it only pops entries whose virtual time is due
     * into the thread-safe handoff.
     *
     * BACKPRESSURE (head-of-line, bounded): drainDueTo() submits ONLY the
     * handoff front. If the front cannot be submitted right now (temporary
     * inbound-bus full), it stays in the handoff and the drain STOPS
     * immediately - no later envelope is submitted before it, nothing is
     * lost, nothing is re-idded and no unbounded retry side queue grows.
     * The worker refills the handoff only when it has space, so the due
     * staging is bounded by `handoffCapacity`. A permanent submit rejection
     * (broken contract) removes the poisoned front, preserves all
     * successors in order and throws a defined CommunicationError.
     *
     * ORDERING: scheduleAt/scheduleAfter are thread-safe. Accepted calls
     * are linearized under the scheduler mutex and each receives a
     * monotonic sequence; (due_time, sequence) defines a total stable
     * delivery order afterwards. For the same due time from the same/serial
     * producer the order is exactly A -> B -> C. The relative order of
     * genuinely concurrent producers is their mutex linearization order -
     * not guaranteed across runs (no other tie-break rule exists).
     */
    class DelayedMessageScheduler
    {
    public:
        /** @param clock          virtual time source (usually
         *                          SteadySchedulerClock; tests inject a fake)
         *  @param handoffCapacity max due envelopes staged between the
         *                          worker and the owner (bounded; must be
         *                          > 0 - a zero capacity could never deliver)
         *  @param sequenceStart   internal tie-break start value (1; the
         *                          overflow test overrides it) */
        explicit DelayedMessageScheduler( SchedulerClock &clock,
                                          std::size_t handoffCapacity = 256,
                                          std::uint64_t sequenceStart = 1 );
        /** Stops and joins the worker thread (no detached zombie left). */
        ~DelayedMessageScheduler();

        DelayedMessageScheduler( const DelayedMessageScheduler & ) = delete;
        DelayedMessageScheduler &operator=( const DelayedMessageScheduler & ) = delete;

        // --- scheduling: thread-safe, store only the envelope ---
        /** Schedules `envelope` at the exact virtual time `dueAt`. Duplicate
         *  due times are ordered by the monotonic scheduler sequence (call
         *  order). Rejected with a loud CommunicationError after shutdown()
         *  (the worker is gone - silently queueing would be silent loss);
         *  sequence overflow never wraps and also throws. */
        void scheduleAt( SchedulerClock::Time dueAt, CommunicationEnvelope envelope );
        /** Convenience: scheduleAfter(delay, envelope) ==
         *  scheduleAt(now() + delay, envelope). */
        void scheduleAfter( std::chrono::steady_clock::duration delay,
                            CommunicationEnvelope envelope );

        // --- owner/game thread (NEVER call from the worker) ---
        /**
         * Hands the currently due envelopes (worker -> thread-safe handoff)
         * into `runtime`'s inbound A queue, HEAD-OF-LINE. MUST run on the
         * owner/UI thread: it calls CommunicationRuntime::submit(), which
         * the worker thread must never call.
         *
         * Backpressure: the front envelope that cannot be submitted right
         * now stays in the handoff and the drain stops immediately - no
         * loss, no re-issue, no later envelope overtakes it. A permanent
         * submit rejection (broken contract) removes the poisoned front and
         * propagates loudly after preserving the remaining successors.
         *
         * Returns the number of envelopes submitted in this call.
         */
        std::size_t drainDueTo( CommunicationRuntime &runtime );

        /** Stops the worker thread and joins it. Idempotent; the destructor
         *  calls it. */
        void shutdown();

        bool running() const;

        // --- introspection (thread-safe reads) ---
        std::size_t scheduledCount() const; // waiting in the priority queue
        std::size_t handoffCount() const;   // due, not yet owner-drained
        std::size_t handoffCapacity() const { return mHandoffCapacity; }
        /** Id of the worker thread, cached at start and read through the
         *  scheduler mutex - never touches the std::thread object, so a
         *  parallel shutdown()/join() can never race this read. */
        std::thread::id workerThreadId() const;

    private:
        struct Entry
        {
            SchedulerClock::Time due;
            std::uint64_t sequence = 0;
            CommunicationEnvelope envelope;
        };
        // Min-heap: earliest due first, equal due resolved by the monotonic
        // sequence. (due_time, sequence) is fully deterministic once the
        // sequence was assigned: scheduling calls are linearized under the
        // scheduler mutex, so for serial producers the order is exactly the
        // call order. Genuinely concurrent producers have no cross-run
        // guaranteed relative order (their order is the mutex linearization
        // order, which is not reproducible across runs).
        struct EarlierEntry
        {
            bool operator()( const Entry &l, const Entry &r ) const
            {
                if( l.due != r.due )
                    return l.due > r.due;
                return l.sequence > r.sequence;
            }
        };

        void workerLoop();

        SchedulerClock &mClock;
        std::size_t mHandoffCapacity;

        mutable std::mutex mMutex;
        std::condition_variable mWorkCv;     // new schedule / stop
        std::condition_variable mHandoffCv; // handoff space freed / stop

        std::priority_queue<Entry, std::vector<Entry>, EarlierEntry> mQueue;
        std::deque<CommunicationEnvelope> mHandoff; // bounded head-of-line stage
        std::uint64_t mSequence;
        bool mStopping = false;
        std::thread mWorker;
        std::thread::id mWorkerThreadId{}; // cached once at start (mMutex-protected)
    };
} // namespace world::communication