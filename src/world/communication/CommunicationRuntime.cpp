#include "world/communication/CommunicationRuntime.h"

#include "world/communication/CommunicationError.h"

namespace world::communication
{
    namespace
    {
        std::string kindName( EnvelopeKind kind )
        {
            switch( kind )
            {
                case EnvelopeKind::Command: return "Command";
                case EnvelopeKind::Query: return "Query";
                case EnvelopeKind::Event: return "Event";
                case EnvelopeKind::Reply: return "Reply";
            }
            return std::to_string( static_cast<int>( kind ) );
        }

        // A slot dimension is a logical id: for context/capability the
        // permanent contract requires namespaced ids; absent/empty is the
        // wildcard. (The receiver is deliberately NOT namespaced - it is a
        // plain logical id, exactly like the envelope contract: non-empty
        // logical ID, no namespace requirement.)
        void requireNamespacedId( const std::optional<std::string> &id, const char *what )
        {
            if( id.has_value() && !id->empty() && !world::isNamespacedId( *id ) )
                throw CommunicationError( std::string( what ) + " '" + *id +
                                         "' must be namespaced as <namespace>:<name>, "
                                         "or be empty/absent for the wildcard" );
        }

        /**
         * RAII/scope guard for the async pump critical phase (MAJOR 1):
         * while a pumpOne()/pumpAll() is active, any reentrant pump attempt
         * throws BEFORE touching A/B or executing a handler. pumpAll()
         * sequences normal pumpOne() calls, each acquiring/releasing the
         * guard - a handler or trace sink can therefore never start a nested
         * pump.
         */
        class AsyncPumpGuard
        {
        public:
            explicit AsyncPumpGuard( bool &active ) : mActive( active )
            {
                if( mActive )
                    throw CommunicationError(
                        "reentrant async pump: pumpOne()/pumpAll() may not be invoked from "
                        "within an active async pump (handler or trace sink); the nested "
                        "pump was rejected without touching A/B" );
                mActive = true;
            }
            ~AsyncPumpGuard() { mActive = false; }
            AsyncPumpGuard( const AsyncPumpGuard & ) = delete;
            AsyncPumpGuard &operator=( const AsyncPumpGuard & ) = delete;

        private:
            bool &mActive;
        };
    } // namespace

    CommunicationRuntime::CommunicationRuntime( std::size_t inboundCapacity,
                                                std::size_t outboundCapacity ) :
        mInbound( inboundCapacity ),
        mOutbound( outboundCapacity )
    {
    }

    void CommunicationRuntime::declareSignal( const std::string &action, EnvelopeKind kind,
                                              PayloadSchema schema )
    {
        requireNoTraceReentrancy();
        // SignalRegistry::declare validates BEFORE it mutates (single
        // operation -> inherently atomic).
        mSignals.declare( action, kind, schema );
    }

    void CommunicationRuntime::registerAction( const std::string &actionId, Handler handler )
    {
        requireNoTraceReentrancy();
        // The public action contract is a stable namespaced id; validation
        // happens before ActionRegistry::insert mutates anything.
        if( !world::isNamespacedId( actionId ) )
            throw CommunicationError( "action id '" + actionId +
                                     "' must be namespaced as <namespace>:<name>" );
        mActions.insert( actionId, std::move( handler ) );
    }

    void CommunicationRuntime::checkSlotRegistration(
        const std::string &action, EnvelopeKind kind,
        const std::optional<std::string> &receiver,
        const std::optional<std::string> &context,
        const std::optional<std::string> &capability, PayloadSchema expectedSchema,
        const std::string &actionId, const OutputContract &contract ) const
    {
        // Pure validation - NOTHING is mutated on this path (failed
        // registrations leave no partial state).
        requirePayloadSchema( expectedSchema, /*allowAny=*/true, "slot" );
        if( !isValidEnvelopeKind( kind ) )
            throw CommunicationError( "cannot register a slot for invalid EnvelopeKind" );
        if( !mSignals.contains( action, kind ) )
            throw CommunicationError( "no signal declared for action '" + action + "' kind '" +
                                     kindName( kind ) + "'; declare the signal first" );
        if( !mActions.find( actionId ) )
            throw CommunicationError( "no action '" + actionId +
                                     "' is registered; register the action first" );
        const PayloadSchema signalSchema = mSignals.schemaOf( action, kind );
        if( !schemaAccepts( signalSchema, expectedSchema ) )
            throw CommunicationError( "slot for action '" + action + "' expects payload schema '" +
                                     schemaName( expectedSchema ) + "' but its signal declares '" +
                                     schemaName( signalSchema ) + "'" );
        // Output contract: kind and schema must be defined values (the
        // contract itself is validated at registration, not at dispatch).
        if( !isValidEnvelopeKind( contract.kind ) )
            throw CommunicationError( "output contract for action '" + action +
                                     "' declares an invalid output EnvelopeKind" );
        requirePayloadSchema( contract.schema, /*allowAny=*/true, "output contract" );
        // Context/capability are namespaced ids (permanent contract); the
        // receiver is a plain logical id and is NOT namespaced.
        requireNamespacedId( context, "slot context" );
        requireNamespacedId( capability, "slot capability" );
        // Duplicate detection BEFORE mutation (slot registry AND router route).
        if( mSlots.contains( action, kind, receiver.value_or( std::string{} ),
                             context.value_or( std::string{} ),
                             capability.value_or( std::string{} ) ) )
            throw CommunicationError( "a slot for action '" + action +
                                     "' of this kind/receiver/context/capability is "
                                     "already registered" );
        if( mRouter.hasRoute( action, kind, receiver, context, capability ) )
            throw CommunicationError( "a router route for action '" + action +
                                     "' of this kind/receiver/context/capability is "
                                     "already bound" );
    }

    void CommunicationRuntime::bindRoute( const std::string &action, EnvelopeKind kind,
                                          const std::optional<std::string> &receiver,
                                          const std::optional<std::string> &context,
                                          const std::optional<std::string> &capability,
                                          const std::string &actionId )
    {
        // Internal execution table: the route dereferences the registered
        // Action at dispatch time, so routing and the semantic Slot always
        // select the same reaction.
        mRouter.registerHandler(
            action, kind,
            [this, actionId]( const CommunicationEnvelope &envelope,
                              std::vector<CommunicationEnvelope> &outputs ) {
                const Handler *reaction = mActions.find( actionId );
                if( !reaction )
                    throw CommunicationError( "bound action '" + actionId +
                                             "' is no longer registered" );
                ( *reaction )( envelope, outputs );
            },
            receiver, context, capability );
    }

    void CommunicationRuntime::registerSlot( const std::string &action, EnvelopeKind kind,
                                             const std::optional<std::string> &receiver,
                                             const std::optional<std::string> &context,
                                             const std::optional<std::string> &capability,
                                             PayloadSchema expectedSchema,
                                             const std::string &actionId,
                                             const std::optional<OutputContract> &contract )
    {
        requireNoTraceReentrancy();
        const OutputContract effective =
            contract.value_or( defaultOutputContract( kind ) );
        // Phase 1: validate everything (no mutation).
        checkSlotRegistration( action, kind, receiver, context, capability, expectedSchema,
                               actionId, effective );
        // Phase 2: commit. All pre-validated conditions hold, so the slot
        // insert cannot fail afterwards; the route binding is rolled back
        // together with the slot on any unexpected failure - a registration
        // is all-or-nothing.
        SlotRegistry::Definition definition;
        definition.action = action;
        definition.kind = kind;
        definition.receiver = receiver.value_or( std::string{} );
        definition.context = context.value_or( std::string{} );
        definition.capability = capability.value_or( std::string{} );
        definition.expected = expectedSchema;
        definition.actionId = actionId;
        definition.contract = effective;
        mSlots.insert( definition );
        try
        {
            bindRoute( action, kind, receiver, context, capability, actionId );
        }
        catch( ... )
        {
            mSlots.erase( definition.action, definition.kind, definition.receiver,
                          definition.context, definition.capability );
            throw;
        }
    }

    void CommunicationRuntime::registerHandler( const std::string &action, EnvelopeKind kind,
                                                Handler handler,
                                                const std::optional<std::string> &receiver,
                                                const std::optional<std::string> &context,
                                                const std::optional<std::string> &capability,
                                                PayloadSchema schema,
                                                const std::optional<OutputContract> &contract )
    {
        requireNoTraceReentrancy();
        const OutputContract effective =
            contract.value_or( defaultOutputContract( kind ) );
        // Phase 1: pure validation (no mutation). The schema is mandatory -
        // the registration path has no silently-invalid "Any" default.
        if( !handler )
            throw CommunicationError( "refusing to register an empty handler for action '" +
                                     action + "'" );
        mSignals.checkCanDeclare( action, kind, schema );
        requireNamespacedId( context, "handler context" );
        requireNamespacedId( capability, "handler capability" );
        if( !isValidEnvelopeKind( effective.kind ) )
            throw CommunicationError( "output contract for action '" + action +
                                     "' declares an invalid output EnvelopeKind" );
        requirePayloadSchema( effective.schema, /*allowAny=*/true, "output contract" );
        if( mSlots.contains( action, kind, receiver.value_or( std::string{} ),
                             context.value_or( std::string{} ),
                             capability.value_or( std::string{} ) ) )
            throw CommunicationError( "a slot for action '" + action +
                                     "' of this kind/receiver/context/capability is "
                                     "already registered" );
        if( mRouter.hasRoute( action, kind, receiver, context, capability ) )
            throw CommunicationError( "a router route for action '" + action +
                                     "' of this kind/receiver/context/capability is "
                                     "already bound" );

        // Phase 2: transactional commit of signal + action + slot + route.
        const std::string actionId = action + "." + std::to_string( mAutoActionCounter++ );
        const bool signalExisted = mSignals.contains( action, kind );
        SlotRegistry::Definition definition;
        definition.action = action;
        definition.kind = kind;
        definition.receiver = receiver.value_or( std::string{} );
        definition.context = context.value_or( std::string{} );
        definition.capability = capability.value_or( std::string{} );
        definition.expected = schema;
        definition.actionId = actionId;
        definition.contract = effective;
        try
        {
            mSignals.declare( action, kind, schema );
            mActions.insert( actionId, std::move( handler ) );
            mSlots.insert( definition );
            bindRoute( action, kind, receiver, context, capability, actionId );
        }
        catch( ... )
        {
            // Best-effort rollback: no partial registration may survive.
            mActions.erase( actionId );
            mSlots.erase( definition.action, definition.kind, definition.receiver,
                          definition.context, definition.capability );
            if( !signalExisted )
                mSignals.erase( action, kind );
            throw;
        }
    }

    const SlotRegistry::Definition *
    CommunicationRuntime::validateInbound( const CommunicationEnvelope &envelope ) const
    {
        // Input boundary: the envelope itself must be a valid message.
        validateEnvelope( envelope );
        // The signal must be declared (unknown actions fail loudly at the bus
        // boundary, not silently).
        if( !mSignals.contains( envelope.action, envelope.kind ) )
            throw CommunicationError( "no signal declared for action '" + envelope.action +
                                     "' kind '" + kindName( envelope.kind ) + "'" );
        const PayloadSchema signalSchema = mSignals.schemaOf( envelope.action, envelope.kind );
        // Defensive: stored signal schemas are always valid (declaration
        // gate), but the boundary validates the enum once more.
        requirePayloadSchema( signalSchema, /*allowAny=*/false, "signal" );
        // The envelope must actually carry the signal's declared payload
        // schema (typed payload contract at the boundary).
        if( !payloadMatches( envelope.payload, signalSchema ) )
            throw CommunicationError( "payload of signal '" + envelope.action +
                                     "' does not match the declared schema '" +
                                     schemaName( signalSchema ) + "'" );
        // Slot resolution (same deterministic priority as the router) for the
        // authorization gate: a capability-guarded slot only executes for a
        // sender that holds the registered grant - a self-asserted envelope
        // capability string is a routing request, never an authorization.
        const std::string cap = envelope.capability.value_or( std::string{} );
        const SlotRegistry::Definition *slot = mSlots.find(
            envelope.action, envelope.kind, envelope.receiver, envelope.context, cap );
        if( !slot )
            throw CommunicationError( "no slot for action '" + envelope.action + "' kind '" +
                                     kindName( envelope.kind ) + "' at receiver '" +
                                     envelope.receiver + "' context '" + envelope.context + "'" );
        if( !slot->capability.empty() &&
            !principalHasCapability( envelope.sender, slot->capability ) )
            throw CommunicationError( "sender '" + envelope.sender + "' is not authorized for "
                                     "capability '" + slot->capability + "'" );
        return slot;
    }

    void CommunicationRuntime::validateOutputsAgainstContract(
        const CommunicationEnvelope &cause, const SlotRegistry::Definition &slot,
        const std::vector<CommunicationEnvelope> &outputs ) const
    {
        const OutputContract &contract = slot.contract;
        if( outputs.size() > contract.maxOutputs )
            throw CommunicationError( "handler for action '" + cause.action + "' produced " +
                                     std::to_string( outputs.size() ) + " output(s); its declared "
                                     "output contract allows at most " +
                                     std::to_string( contract.maxOutputs ) );
        for( const auto &output : outputs )
        {
            if( output.kind != contract.kind )
                throw CommunicationError( "handler for action '" + cause.action +
                                         "' produced an output of kind '" +
                                         kindName( output.kind ) + "'; its output contract "
                                         "requires kind '" + kindName( contract.kind ) + "'" );
            if( !payloadMatches( output.payload, contract.schema ) )
                throw CommunicationError( "handler for action '" + cause.action +
                                         "' produced an output whose payload does not match "
                                         "the declared output schema '" +
                                         schemaName( contract.schema ) + "'" );
            if( contract.requireCorrelation &&
                ( !output.correlationId.has_value() ||
                  *output.correlationId != cause.messageId ) )
                throw CommunicationError( "handler for action '" + cause.action +
                                         "' produced an output that does not correlate with "
                                         "the cause message " + std::to_string( cause.messageId ) );
        }
    }

    CommunicationRouter::DispatchResult
    CommunicationRuntime::dispatch( const CommunicationEnvelope &envelope )
    {
        requireNoTraceReentrancy();
        // Synchronous gameplay/input convenience adapter: validate first
        // (nothing executes on failure), then run EXACTLY this envelope. The
        // outputs are validated against the matched slot's OutputContract and
        // delivered in the returned DispatchResult - never staged onto the
        // outbound queue (one delivery route per message). Independent of the
        // outbound capacity (MAJOR 2) and allowed during an active async pump
        // (it does not change the A/B reservation).
        const SlotRegistry::Definition *slot = validateInbound( envelope );
        emitTrace( envelope ); // accepted cause: passed validation, about to execute
        CommunicationRouter::DispatchResult result = mRouter.dispatch( envelope );
        validateOutputsAgainstContract( envelope, *slot, result.replies );
        // Accepted-bus trace: only contract-valid outputs appear as successful
        // bus outputs (the raw router keeps its own attempt-level trace).
        for( const auto &output : result.replies )
            emitTrace( output );
        return result;
    }

    bool CommunicationRuntime::submit( const CommunicationEnvelope &envelope )
    {
        requireNoTraceReentrancy();
        // Bus boundary: the envelope is fully validated BEFORE it may enter
        // the inbound queue. Invalid -> CommunicationError, queue unchanged.
        const SlotRegistry::Definition *slot = validateInbound( envelope );
        // Permanent undeliverability gate (MAJOR 2): a message whose declared
        // output contract exceeds the outbound CAPACITY can never be
        // processed on the async path - it must never block the queue head.
        // (Temporary backpressure - available < maxOutputs <= capacity - is
        // handled at pump time and keeps the retry semantics.)
        if( slot->contract.maxOutputs > mOutbound.capacity() )
            throw CommunicationError(
                "message " + std::to_string( envelope.messageId ) + " (action '" +
                envelope.action + "') can never be delivered on the async path: its "
                "output contract allows up to " + std::to_string( slot->contract.maxOutputs ) +
                " output(s) but the outbound capacity is " +
                std::to_string( mOutbound.capacity() ) + "; it was NOT queued" );
        return mInbound.tryPush( envelope );
    }

    CommunicationRouter::DispatchResult CommunicationRuntime::pumpOne()
    {
        requireNoTraceReentrancy();
        // Reentrancy guard (MAJOR 1): covers the whole critical phase below
        // (peek -> validation -> capacity preflight -> pop -> handler
        // execution -> OutputContract validation -> outbound commit). A
        // nested pump attempt throws before touching anything.
        AsyncPumpGuard guard( mAsyncPumpActive );
        // 1. Only LOOK at the oldest inbound envelope - it is not consumed
        //    yet (pre-flight contract: backpressure must never run the
        //    handler and never lose the pending message).
        const CommunicationEnvelope *peeked = mInbound.peek();
        if( !peeked )
            throw CommunicationError( "no pending inbound envelope to pump" );
        // 2. Full inbound validation (envelope, signal, payload schema, slot,
        //    capability authorization). The matched slot carries the declared
        //    OutputContract.
        const SlotRegistry::Definition *slot = validateInbound( *peeked );
        // 3.+4. Backpressure pre-flight BEFORE any side effect: the outbound
        //    queue must be able to hold the contract's maximum output count.
        //    The handler may produce fewer, never more.
        if( slot->contract.maxOutputs > mOutbound.available() )
            throw CommunicationError(
                "outbound backpressure: message " + std::to_string( peeked->messageId ) +
                " (action '" + peeked->action + "') needs up to " +
                std::to_string( slot->contract.maxOutputs ) + " output slot(s) but only " +
                std::to_string( mOutbound.available() ) +
                " are free; the envelope stays pending and the handler did NOT run" );
        // 5./6. Pre-flight succeeded: only now consume the envelope.
        const std::optional<CommunicationEnvelope> envelope = mInbound.pop();
        if( !envelope )
            throw CommunicationError( "no pending inbound envelope to pump" ); // unreachable
        emitTrace( *envelope ); // accepted cause: passed validation, about to execute
        // 7. Execute. The router collects the outputs and validates their
        //    structure (MAJOR 4) plus Event fire-and-forget.
        CommunicationRouter::DispatchResult result = mRouter.dispatch( *envelope );
        // 8. Validate the produced outputs against the slot's OutputContract
        //    (count, kind, payload schema, correlation).
        validateOutputsAgainstContract( *envelope, *slot, result.replies );
        // 9. Commit atomically: the pre-flight guaranteed the capacity, so
        //    every output fits and no partial delivery can occur. A `false`
        //    from tryPush is an internal invariant violation here (preflight
        //    + reentrancy guard) and must be reported loudly, never dropped.
        for( const auto &output : result.replies )
        {
            if( !mOutbound.tryPush( output ) )
                throw CommunicationError(
                    "internal invariant violation: outbound commit failed although the "
                    "capacity pre-flight and the reentrancy guard succeeded" );
            emitTrace( output ); // accepted-bus trace: contract-valid output committed
        }
        // ONE delivery route: the queue path delivered into B; the returned
        // result must not deliver them a second time.
        result.replies.clear();
        result.handled = true;
        return result;
    }

    void CommunicationRuntime::pumpAll()
    {
        requireNoTraceReentrancy();
        while( !mInbound.empty() )
            pumpOne();
    }

    std::optional<CommunicationEnvelope> CommunicationRuntime::nextOutput()
    {
        requireNoTraceReentrancy();
        return mOutbound.pop();
    }

    void CommunicationRuntime::grantCapability( const std::string &principal,
                                                const std::string &capability )
    {
        requireNoTraceReentrancy();
        if( principal.empty() )
            throw CommunicationError( "cannot grant a capability to an empty principal" );
        if( !world::isNamespacedId( capability ) )
            throw CommunicationError( "capability '" + capability +
                                     "' must be namespaced as <namespace>:<name>" );
        mCapabilities[principal].insert( capability );
    }

    bool CommunicationRuntime::principalHasCapability( const std::string &principal,
                                                       const std::string &capability ) const
    {
        const auto it = mCapabilities.find( principal );
        return it != mCapabilities.end() && it->second.count( capability ) != 0;
    }

    CommunicationEnvelope
    CommunicationRuntime::makeReply( const CommunicationEnvelope &cause,
                                      CommandResultPayload result )
    {
        // Controlled reply-id minting: guarded against trace reentrancy like
        // nextMessageId(); the reply's id comes from the runtime's private
        // sequence and the correlation is exactly cause.messageId. The
        // delegate keeps the exact M02 reply semantics.
        requireNoTraceReentrancy();
        return ::world::communication::makeReply( cause, std::move( result ), mIds );
    }

    void CommunicationRuntime::requireNoTraceReentrancy() const
    {
        // MAJOR (M03 Round 1): while a runtime trace sink executes, no
        // mutating/consuming runtime operation may run. The exception thrown
        // here is contained and counted by emitTrace(), so the outer bus
        // operation continues normally.
        if( mTraceActive )
            throw CommunicationError( "CommunicationRuntime mutation/consumption is not "
                                     "allowed from a trace sink" );
    }

    void CommunicationRuntime::emitTrace( const CommunicationEnvelope &envelope )
    {
        // Purely observational trace: a sink exception is contained and
        // counted, never propagated into dispatch()/pumpOne(). While the
        // sink runs, mTraceActive blocks every mutating/consuming runtime
        // API (requireNoTraceReentrancy), so a trace observer can never
        // steal outputs, inject messages, run handlers or reconfigure the
        // bus - and the outer bus operation continues normally.
        if( !mTrace || mTraceActive )
            return;
        mTraceActive = true;
        try
        {
            mTrace( envelope );
        }
        catch( ... )
        {
            ++mTraceFailures;
        }
        mTraceActive = false;
    }
} // namespace world::communication