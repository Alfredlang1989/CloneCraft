#include "world/communication/DelayedMessageScheduler.h"

namespace world::communication
{
    namespace
    {
        using Time = SchedulerClock::Time;

        /** Checked absolute due-time addition (Round-2 approved fix): a
         *  user-controlled delay must never wrap the time point domain.
         *  `now` is a valid scheduler time; a non-negative `delay` that
         *  exceeds the remaining representable range up to Time::max() is
         *  rejected loudly instead of wrapping (no UB, no silent wrap). */
        Time checkedDueTime( Time now, std::chrono::steady_clock::duration delay )
        {
            if( delay > Time::max() - now )
                throw CommunicationError(
                    "delayed message scheduler: delay exceeds the representable scheduler "
                    "time range from now; the due time would wrap" );
            return now + delay;
        }
    } // namespace

    DelayedMessageScheduler::DelayedMessageScheduler( SchedulerClock &clock,
                                                      std::size_t handoffCapacity,
                                                      std::uint64_t sequenceStart ) :
        mClock( clock ),
        mHandoffCapacity( handoffCapacity ),
        mSequence( sequenceStart )
    {
        // A zero handoff capacity could never deliver anything - reject the
        // construction BEFORE a worker is started (no thread left behind).
        if( mHandoffCapacity == 0u )
            throw CommunicationError( "delayed message scheduler: handoffCapacity must "
                                     "be > 0 (a zero capacity can never deliver)" );
        mWorker = std::thread( &DelayedMessageScheduler::workerLoop, this );
        // Cache the worker id ONCE at start: introspection reads only this
        // scheduler-owned value under the mutex, never the live std::thread
        // object (safe against a concurrent shutdown()/join()).
        mWorkerThreadId = mWorker.get_id();
    }

    DelayedMessageScheduler::~DelayedMessageScheduler()
    {
        shutdown();
    }

    void DelayedMessageScheduler::scheduleAt( Time dueAt, CommunicationEnvelope envelope )
    {
        {
            std::lock_guard<std::mutex> lock( mMutex );
            // After shutdown() the worker is gone: queueing silently would
            // be silent loss - loud, defined rejection. No sequence is
            // consumed and no queue is touched on this path.
            if( mStopping )
                throw CommunicationError( "delayed message scheduler: scheduleAt() after "
                                         "shutdown() is rejected (the worker is gone - "
                                         "queueing would silently lose the message)" );
            // The sequence must never silently wrap: 0/duplicate sequences
            // would break the deterministic tie-break (loud, defined error).
            if( mSequence == std::numeric_limits<std::uint64_t>::max() )
                throw CommunicationError( "scheduler sequence overflow: no monotonic "
                                         "sequence number left (never wraps)" );
            mQueue.push( Entry{ dueAt, mSequence++, std::move( envelope ) } );
        }
        // Wake the worker immediately: a new (possibly earlier) entry must
        // re-evaluate its wait target - no busy waiting, no missed wakeup.
        mClock.interrupt();
        mWorkCv.notify_all();
    }

    void DelayedMessageScheduler::scheduleAfter( std::chrono::steady_clock::duration delay,
                                                 CommunicationEnvelope envelope )
    {
        // Checked absolute time addition: mClock.now() + delay must stay
        // representable (the delay is user-controlled input, Round-2 fix).
        scheduleAt( checkedDueTime( mClock.now(), delay ), std::move( envelope ) );
    }

    void DelayedMessageScheduler::workerLoop()
    {
        std::unique_lock<std::mutex> lock( mMutex );
        for( ;; )
        {
            if( mStopping )
                return;

            // Move everything that is due into the thread-safe handoff
            // (bounded: wait for owner space instead of growing silently).
            while( !mQueue.empty() && mQueue.top().due <= mClock.now() &&
                   mHandoff.size() < mHandoffCapacity )
            {
                Entry entry = mQueue.top(); // transportable copy (top is const)
                mQueue.pop();
                mHandoff.push_back( std::move( entry.envelope ) );
            }
            if( mStopping )
                return;

            if( mHandoff.size() >= mHandoffCapacity )
            {
                // Everything pending is due but the handoff is full: wait
                // for the owner to drain a slot.
                mHandoffCv.wait( lock, [&] {
                    return mStopping || mHandoff.size() < mHandoffCapacity;
                } );
            }
            else if( !mQueue.empty() )
            {
                // Wait (interruptibly, pending-safe) until the earliest due
                // time - a later earlier schedule wakes this wait.
                const Time until = mQueue.top().due;
                lock.unlock();
                mClock.waitUntil( until );
                lock.lock();
            }
            else
            {
                // Nothing scheduled: wait for new work or shutdown.
                mWorkCv.wait( lock, [&]{ return mStopping || !mQueue.empty(); } );
            }
        }
    }

    std::size_t DelayedMessageScheduler::drainDueTo( CommunicationRuntime &runtime )
    {
        // HEAD-OF-LINE, bounded (MAJOR 2): only the handoff front is
        // attempted and the front STAYS IN the handoff while submit() runs
        // (a deque reference stays valid across back-insertions by the
        // worker). The size therefore never drops below capacity during a
        // submit, so the worker can never overshoot the bound. A temporarily
        // full inbound bus keeps the front in the handoff and stops the
        // drain - no later envelope overtakes it and no unbounded retry side
        // queue ever grows. Runtime calls are made WITHOUT holding the
        // scheduler mutex.
        std::size_t submitted = 0;
        for( ;; )
        {
            const CommunicationEnvelope *candidate = nullptr;
            {
                std::lock_guard<std::mutex> lock( mMutex );
                if( mHandoff.empty() )
                    break;
                candidate = &mHandoff.front(); // kept in place during submit
            }

            bool accepted = false;
            try
            {
                accepted = runtime.submit( *candidate );
            }
            catch( const CommunicationError &error )
            {
                // Permanent contract rejection: remove the poisoned front,
                // preserve all successors in the handoff (in order).
                {
                    std::lock_guard<std::mutex> lock( mMutex );
                    mHandoff.pop_front();
                }
                mHandoffCv.notify_all();
                throw CommunicationError(
                    std::string( "delayed message scheduler: permanent submit rejection: " ) +
                    error.what() );
            }
            if( accepted )
            {
                // Slot freed: remove the front and let the worker refill.
                {
                    std::lock_guard<std::mutex> lock( mMutex );
                    mHandoff.pop_front();
                }
                ++submitted;
                mHandoffCv.notify_all();
                continue;
            }
            // Temporary backpressure: the front stays in the handoff, the
            // drain stops immediately (head-of-line).
            break;
        }
        mHandoffCv.notify_all();
        return submitted;
    }

    void DelayedMessageScheduler::shutdown()
    {
        {
            std::lock_guard<std::mutex> lock( mMutex );
            if( mStopping )
                return;
            mStopping = true;
        }
        // Wake every possible wait state, then join - no detached zombie.
        mClock.interrupt();
        mWorkCv.notify_all();
        mHandoffCv.notify_all();
        if( mWorker.joinable() )
            mWorker.join();
    }

    bool DelayedMessageScheduler::running() const
    {
        // Derived from scheduler state only - the worker exits solely through
        // mStopping, so "running" == "not stopping". Never mWorker.joinable():
        // a concurrent shutdown()/join() can therefore never race this read.
        std::lock_guard<std::mutex> lock( mMutex );
        return !mStopping;
    }

    std::thread::id DelayedMessageScheduler::workerThreadId() const
    {
        // Reads the cached id, not mWorker.get_id() - decoupled from the
        // std::thread object, so a concurrent shutdown()/join() is safe.
        std::lock_guard<std::mutex> lock( mMutex );
        return mWorkerThreadId;
    }

    std::size_t DelayedMessageScheduler::scheduledCount() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mQueue.size();
    }

    std::size_t DelayedMessageScheduler::handoffCount() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mHandoff.size();
    }
} // namespace world::communication
