#pragma once

#include "world/communication/BoundedEnvelopeQueue.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRegistries.h"
#include "world/communication/CommunicationRouter.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace world::communication
{
    /**
     * The production communication bus (M03 Round 1).
     *
     * One runtime context owns:
     *  - the single MessageIdSource (MAJOR 1): every producer in this context
     *    draws its ids from the SAME monotonic source, so ids are unique by
     *    construction; the source is non-copyable and overflow-guarded;
     *  - the Signal/Slot/Action registries (MAJOR 2): real semantic registry
     *    structures; the router's route map stays the internal binding /
     *    execution table (an allowed internal optimization, never the
     *    contract);
     *  - bounded A (inbound) / B (outbound) queues (MAJOR 3): the ASYNC
     *    path (submit()/pump*()/nextOutput()) stages every envelope through
     *    the bounded queues; overflow is loud backpressure, never silent
     *    growth or silent drops;
     *  - the capability authorization store (MAJOR 5): a sender's entitlement
     *    comes from registered grants (grantCapability), NEVER from a
     *    capability string the sender writes into its own envelope;
     *  - bidirectional boundary validation (MAJOR 4 + typed payload schemas +
     *    Event fire-and-forget): the envelope is validated on the way in and
     *    every handler output is validated on the way out.
     *
     * EXACTLY ONE DELIVERY ROUTE PER MESSAGE:
     *  - synchronous dispatch(): the gameplay/input convenience adapter.
     *    It validates, executes NOW via the router and delivers the handler
     *    outputs IN THE RETURNED DispatchResult; the A/B queues are never
     *    touched on this path. The matched slot's declared OutputContract
     *    applies (without any queue capacity concern).
     *  - asynchronous submit()/pump*(): async producers stage envelopes onto
     *    the inbound A queue; pumpOne()/pumpAll() execute and deliver the
     *    outputs EXCLUSIVELY onto the outbound B queue, consumed via
     *    nextOutput(). The returned DispatchResult of pump() carries the
     *    outputs NOT - they are not delivered twice.
     * A message and its produced outputs are therefore never consumable on
     * both paths, and dispatch() always runs exactly the envelope it was
     * given (older queue entries stay pending).
     *
     * OUTPUT CONTRACT + PRE-EXECUTION BACKPRESSURE (MAJOR 1+3):
     * The matched slot declares its OutputContract (maxOutputs, output kind,
     * output payload schema, correlation requirement). pumpOne() PEEKS the
     * oldest inbound envelope, validates it, determines the slot/contract and
     * checks `mOutbound.available() >= maxOutputs` BEFORE the handler runs.
     * If the capacity is insufficient: defined CommunicationError, the
     * handler does NOT run (no side effects), the envelope STAYS in A, and
     * nothing is delivered. Only after a successful pre-flight is the
     * envelope consumed and executed; produced outputs are validated against
     * the contract and committed atomically into B.
     *
     * REENTRANCY (MAJOR 1): the async pump is guarded by an RAII scope guard
     * for its whole critical phase. A handler or trace sink that calls
     * pumpOne()/pumpAll() while a pump is already active gets a defined
     * CommunicationError BEFORE anything is touched - no nested handler
     * execution, no nested consumption, no B change by the nested attempt.
     * submit() from within a handler stays allowed (it only stages new work
     * into A), and the synchronous dispatch() stays allowed (it does not
     * change the A/B reservation).
     *
     * PERMANENT UNDELIVERABILITY (MAJOR 2): a message whose OutputContract
     * (maxOutputs) exceeds the OUTBOUND CAPACITY can never be processed on
     * this runtime's async path. submit() rejects it BEFORE it enters A (it
     * can never block the queue head); temporary backpressure (available <
     * maxOutputs <= capacity) keeps the current retry semantics. The
     * synchronous dispatch() is independent of the outbound capacity.
     *
     * ROUND 2 CONTRACT: the delayed-message worker must not touch these
     * single-threaded queues. Due envelopes travel worker -> thread-safe
     * handoff -> owner thread, which then calls submit()/dispatch() like any
     * producer. No thread synchronization is built into this class.
     */
    class CommunicationRuntime
    {
    public:
        CommunicationRuntime( std::size_t inboundCapacity, std::size_t outboundCapacity );
        CommunicationRuntime( const CommunicationRuntime & ) = delete;
        CommunicationRuntime &operator=( const CommunicationRuntime & ) = delete;
        CommunicationRuntime( CommunicationRuntime && ) = delete;
        CommunicationRuntime &operator=( CommunicationRuntime && ) = delete;

        // --- identity (MAJOR 1): one source per runtime context ---
        /** The ONLY general producer access to message ids: mints the next
         *  unique id of this runtime context. Guarded by
         *  requireNoTraceReentrancy() - a trace sink can never consume ids.
         *  There is deliberately NO public raw MessageIdSource accessor:
         *  the runtime owns its id sequence exclusively (MAJOR, M03
         *  Round 1). */
        std::uint64_t nextMessageId()
        {
            requireNoTraceReentrancy();
            return mIds.next();
        }

        /** Controlled reply factory: mints the reply's message id from the
         *  runtime's single private id source (guarded like nextMessageId)
         *  and keeps the exact M02 reply semantics (correlationId ==
         *  cause.messageId, logical reply addressing, valid by
         *  construction). Production handlers with a CommunicationRuntime
         *  use this instead of a raw id source. The free
         *  makeReply(cause, result, MessageIdSource&) stays for the
         *  legacy/M02 raw-router path, which owns a separate id source. */
        CommunicationEnvelope makeReply( const CommunicationEnvelope &cause,
                                         CommandResultPayload result );

        // --- registries (MAJOR 2) ---
        void declareSignal( const std::string &action, EnvelopeKind kind,
                            PayloadSchema schema );
        void registerAction( const std::string &actionId, Handler handler );
        void registerSlot( const std::string &action, EnvelopeKind kind,
                           const std::optional<std::string> &receiver,
                           const std::optional<std::string> &context,
                           const std::optional<std::string> &capability,
                           PayloadSchema expectedSchema, const std::string &actionId,
                           const std::optional<OutputContract> &contract = std::nullopt );
        /** Convenience: declares the Signal, registers the Action, registers
         *  the Slot and binds the router route in ONE transactional call.
         *  The payload schema is MANDATORY (the registration path has no
         *  legitimately invalid "any" default). The optional OutputContract
         *  defaults to the kind-based contract (Command/Query: one correlated
         *  Reply; Event/Reply: fire-and-forget). A failed registration leaves
         *  no state behind. */
        void registerHandler( const std::string &action, EnvelopeKind kind, Handler handler,
                              const std::optional<std::string> &receiver,
                              const std::optional<std::string> &context,
                              const std::optional<std::string> &capability,
                              PayloadSchema schema,
                              const std::optional<OutputContract> &contract = std::nullopt );

        // --- bounded A/B dispatch (MAJOR 3) ---
        /** Bus boundary: validates the envelope BEFORE it is stored, then
         *  stages it onto the inbound A queue. Throws CommunicationError for
         *  an invalid envelope (the queue stays unchanged); returns false
         *  when the queue is FULL (loud backpressure; the envelope is not
         *  stored). */
        bool submit( const CommunicationEnvelope &envelope );
        /** Pops the oldest inbound envelope and executes it; its outputs are
         *  delivered EXCLUSIVELY onto the outbound B queue (nextOutput()).
         *  The envelope is PEEKED, validated and capacity-pre-flighted BEFORE
         *  it is consumed: on backpressure the handler does not run, the
         *  envelope stays pending and nothing is delivered. Throws when no
         *  inbound envelope is pending or on any bus validation failure. */
        CommunicationRouter::DispatchResult pumpOne();
        void pumpAll();
        /** Synchronous gameplay/input convenience adapter with its OWN
         *  delivery route: validates and then executes EXACTLY the passed
         *  envelope immediately; the outputs are validated against the
         *  matched slot's OutputContract and returned in the DispatchResult -
         *  they are never staged onto the outbound queue. Pending inbound
         *  queue entries are untouched. */
        CommunicationRouter::DispatchResult dispatch( const CommunicationEnvelope &envelope );
        /** Pops the oldest validated output (FIFO) of the async path;
         *  std::nullopt when the outbound queue is empty. */
        std::optional<CommunicationEnvelope> nextOutput();

        std::size_t pendingInbound() const { return mInbound.size(); }
        std::size_t pendingOutbound() const { return mOutbound.size(); }
        std::size_t inboundCapacity() const { return mInbound.capacity(); }
        std::size_t outboundCapacity() const { return mOutbound.capacity(); }

        // --- capability authorization (MAJOR 5) ---
        /** Registers that `principal` (a logical sender id) is authorized for
         *  a namespaced capability. This registered grant - not the envelope
         *  string - is what authorization checks resolve. */
        void grantCapability( const std::string &principal, const std::string &capability );
        bool principalHasCapability( const std::string &principal,
                                     const std::string &capability ) const;

        // --- tracing (accepted bus trace, purely observational) ---
        /** The production trace sink sees the accepted cause envelope (once
         *  it passed validation and is being executed) and then every
         *  CONTRACT-ACCEPTED output, in order, read-only. Contract-invalid
         *  handler outputs never appear in this trace. (The raw router keeps
         *  its own low-level "attempt trace"; the runtime trace is the
         *  accepted bus trace.)
         *
         * The trace is a READ-ONLY CommunicationRuntime observer:
         *  - sink exceptions are contained (emitTrace) and can never prevent
         *    a valid message, lose an inbound envelope, abort handler
         *    execution, turn a successful delivery into a bus error or change
         *    queue state; traceFailureCount() reports contained failures;
         *  - while a sink runs, reentrant MUTATING/CONSUMING runtime
         *    operations are rejected with a CommunicationError (contained
         *    and counted): dispatch(), submit(), pumpOne(), pumpAll(),
         *    nextOutput(), nextMessageId() and the configuration mutations
         *    declareSignal()/registerAction()/registerSlot()/registerHandler()/
         *    grantCapability()/setTraceSink(). A sink can therefore never
         *    steal outputs, inject messages, run handlers or reconfigure the
         *    bus. Read-only introspection (pending*, capacity/count getters,
         *    principalHasCapability, traceFailureCount) stays available.
         *
         * This does NOT claim the trace can never change arbitrary external
         * state: any C++ code outside the CommunicationRuntime API contract
         * (e.g. mutable captures) is of course not sandboxed. */
        void setTraceSink( std::function<void( const CommunicationEnvelope & )> sink )
        {
            requireNoTraceReentrancy();
            mTrace = std::move( sink );
        }
        std::size_t traceFailureCount() const { return mTraceFailures; }

        std::size_t signalCount() const { return mSignals.size(); }
        std::size_t slotCount() const { return mSlots.size(); }
        std::size_t actionCount() const { return mActions.size(); }
        std::size_t routeCount() const { return mRouter.routeCount(); }

    private:
        /** Validates the envelope contract, signal, payload schema, slot and
         *  capability authorization WITHOUT executing or mutating anything.
         *  Throws CommunicationError on the first violation and returns the
         *  matched slot (whose OutputContract governs execution/delivery).
         *  Shared by the synchronous dispatch and the async submit/pump
         *  boundary. */
        const SlotRegistry::Definition *
        validateInbound( const CommunicationEnvelope &envelope ) const;
        /** The pure validation of a slot registration (signal exists, action
         *  exists, schema compatibility, id rules, duplicates, output
         *  contract) used before ANY mutation - the atomicity gate of
         *  registerSlot(). */
        void checkSlotRegistration( const std::string &action, EnvelopeKind kind,
                                    const std::optional<std::string> &receiver,
                                    const std::optional<std::string> &context,
                                    const std::optional<std::string> &capability,
                                    PayloadSchema expectedSchema,
                                    const std::string &actionId,
                                    const OutputContract &contract ) const;
        /** Validates produced outputs against the matched slot's declared
         *  OutputContract: count, kind, payload schema and - when the
         *  contract requires it - the exact correlation with the cause
         *  message. Throws CommunicationError on the first violation. */
        void validateOutputsAgainstContract(
            const CommunicationEnvelope &cause, const SlotRegistry::Definition &slot,
            const std::vector<CommunicationEnvelope> &outputs ) const;
        /** Binds the router route that dereferences `actionId`. */
        void bindRoute( const std::string &action, EnvelopeKind kind,
                        const std::optional<std::string> &receiver,
                        const std::optional<std::string> &context,
                        const std::optional<std::string> &capability,
                        const std::string &actionId );
        /**
         * Centralized, purely observational trace emission. Without a sink it
         * is a no-op; a sink exception is CONTAINED (counted, then ignored).
         * While a sink runs, mTraceActive blocks every mutating/consuming
         * runtime API (see setTraceSink) - a trace observer can never
         * manipulate the CommunicationRuntime itself.
         */
        void emitTrace( const CommunicationEnvelope &envelope );
        /** Guard entry of every mutating/consuming runtime API: rejects calls
         *  originating from a running trace sink with a defined
         *  CommunicationError (contained and counted by emitTrace). */
        void requireNoTraceReentrancy() const;

        CommunicationRouter mRouter;
        MessageIdSource mIds;
        BoundedEnvelopeQueue mInbound;
        BoundedEnvelopeQueue mOutbound;
        SignalRegistry mSignals;
        SlotRegistry mSlots;
        ActionRegistry mActions;
        std::map<std::string, std::set<std::string>> mCapabilities;
        std::function<void( const CommunicationEnvelope & )> mTrace;
        std::size_t mTraceFailures = 0;
        /** True while a trace sink executes: reentrant mutating/consuming
         *  runtime operations are rejected (read-only trace observer). */
        bool mTraceActive = false;
        std::uint64_t mAutoActionCounter = 0;
        /** Reentrancy guard: set while an async pump (pumpOne/pumpAll) is
         *  inside its critical phase (peek -> validation -> preflight -> pop
         *  -> handler execution -> contract validation -> commit). A nested
         *  pump attempt throws BEFORE touching A/B. */
        bool mAsyncPumpActive = false;
    };
} // namespace world::communication