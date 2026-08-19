#pragma once

#include "world/communication/CommunicationEnvelope.h"

#include <cstddef>
#include <deque>
#include <optional>

namespace world::communication
{
    /**
     * Bounded transport queue for CommunicationEnvelopes (M03, bounded A/B
     * queues).
     *
     * A fixed-capacity FIFO: the communication subsystem stages envelopes
     * through such queues (inbound A, outbound B) so no component can grow
     * an unbounded inbox/outbox. Overflow is loud backpressure: tryPush()
     * returns false instead of silently dropping or reallocating - the caller
     * decides (reject, shed or re-route) without ever storing a function
     * pointer or Lua ref in a message.
     *
     * Single-threaded by design in M03. ROUND 2 CONTRACT (delayed-message
     * worker): the worker thread must NEVER access this queue directly.
     * Due envelopes travel worker -> thread-safe handoff (a synchronization
     * primitive owned by the timer subsystem) -> owner thread, which then
     * uses CommunicationRuntime::submit()/dispatch() exactly like every
     * other producer. This queue itself gains no thread safety in Round 2.
     */
    class BoundedEnvelopeQueue
    {
    public:
        explicit BoundedEnvelopeQueue( std::size_t capacity ) : mCapacity( capacity )
        {
        }

        /** Pushes a copy; false (and no state change) when the queue is full.
         *  Capacity 0 rejects every push - an explicit, deterministic limit. */
        bool tryPush( const CommunicationEnvelope &envelope )
        {
            if( mItems.size() >= mCapacity )
                return false;
            mItems.push_back( envelope );
            return true;
        }

        /** Pops the oldest envelope (FIFO); std::nullopt when empty. */
        std::optional<CommunicationEnvelope> pop()
        {
            if( mItems.empty() )
                return std::nullopt;
            CommunicationEnvelope front = std::move( mItems.front() );
            mItems.pop_front();
            return front;
        }

        /** Non-mutating view of the oldest envelope (FIFO); nullptr when
         *  empty. Lets the runtime PRE-FLIGHT the next message (validate +
         *  capacity check) BEFORE consuming it - the envelope stays in the
         *  queue until its execution is actually committed (M03 Round 1). */
        const CommunicationEnvelope *peek() const
        {
            return mItems.empty() ? nullptr : &mItems.front();
        }

        /** Free slots; 0 for a full (or capacity-0) queue. Lets a staged
         *  delivery check the atomic capacity BEFORE committing any item, so
         *  no partial enqueue can ever occur (M03 Round 1). */
        std::size_t available() const
        {
            return mItems.size() >= mCapacity ? 0 : mCapacity - mItems.size();
        }

        bool empty() const { return mItems.empty(); }
        bool full() const { return mItems.size() >= mCapacity; }
        std::size_t size() const { return mItems.size(); }
        std::size_t capacity() const { return mCapacity; }

    private:
        std::size_t mCapacity;
        std::deque<CommunicationEnvelope> mItems;
    };
} // namespace world::communication