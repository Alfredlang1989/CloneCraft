#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace world::communication
{
    /**
     * Minimal testable time abstraction for the delayed-message scheduler
     * (M03 Round 2).
     *
     * One virtual "scheduler time" domain: `now()` is monotonic, and
     * `waitUntil(until)` blocks the calling thread until the virtual time
     * reaches `until` OR `interrupt()` is called (new earlier schedule,
     * handoff space freed or shutdown). Production always uses
     * SteadySchedulerClock (std::chrono::steady_clock - OS time corrections
     * can never jump a running timer); tests inject a deterministic fake
     * clock with instant advance.
     *
     * INTERRUPT CONTRACT (pending-safe): an interrupt that arrives
     * immediately BEFORE the actual waitUntil() entry must still abort that
     * following wait - it must never be cleared by entering waitUntil().
     * Both implementations use a consumable pending-interrupt flag set and
     * read under the same mutex as waitUntil(): waitUntil() consumes a
     * pending interrupt and returns immediately, otherwise it waits until
     * `until` or a new pending interrupt and consumes it afterwards. No busy
     * waiting anywhere.
     */
    class SchedulerClock
    {
    public:
        using Time = std::chrono::steady_clock::time_point;

        virtual ~SchedulerClock() = default;

        /** Current virtual time (thread-safe). */
        virtual Time now() const = 0;

        /** Blocks until the virtual time reaches `until` or a pending
         *  interrupt arrives (pending-safe, see class doc). No busy
         *  waiting. */
        virtual void waitUntil( Time until ) = 0;

        /** Arms a pending interrupt and wakes any active waitUntil() (new
         *  earlier schedule, shutdown). Thread-safe, callable while a wait
         *  is in progress. */
        virtual void interrupt() = 0;
    };

    /**
     * Production time source: std::chrono::steady_clock behind an
     * interruptible wait. `waitUntil` targets the same steady clock domain,
     * so a scheduled timer is never shifted by wall-clock/OS time changes.
     */
    class SteadySchedulerClock final : public SchedulerClock
    {
    public:
        Time now() const override { return std::chrono::steady_clock::now(); }

        void waitUntil( Time until ) override
        {
            std::unique_lock<std::mutex> lock( mMutex );
            // Pending-safe: an interrupt armed before this entry is consumed
            // here and must not be lost to a fresh wait.
            if( mPendingInterrupt )
            {
                mPendingInterrupt = false;
                return;
            }
            // Otherwise wait until the target OR a new pending interrupt
            // (a notify alone never shortens the original target).
            mCv.wait_until( lock, until, [this] { return mPendingInterrupt; } );
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

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;
        bool mPendingInterrupt = false;
    };
} // namespace world::communication