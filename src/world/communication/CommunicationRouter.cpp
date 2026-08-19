#include "world/communication/CommunicationRouter.h"

namespace world::communication
{
    namespace
    {
        bool validKind( EnvelopeKind kind )
        {
            switch( kind )
            {
                case EnvelopeKind::Command:
                case EnvelopeKind::Query:
                case EnvelopeKind::Event:
                case EnvelopeKind::Reply:
                    return true;
            }
            return false;
        }
    } // namespace

    void CommunicationRouter::registerHandler(
        const std::string &action, EnvelopeKind kind, Handler handler,
        const std::optional<std::string> &receiver,
        const std::optional<std::string> &context,
        const std::optional<std::string> &capability )
    {
        // Registration is itself validated (M03 gate): a broken route can
        // never be bound to the bus.
        if( !validKind( kind ) )
            throw CommunicationError( "cannot register handler for invalid EnvelopeKind" );
        if( !world::isNamespacedId( action ) )
            throw CommunicationError( "handler action '" + action +
                                     "' must be namespaced as <namespace>:<name>" );
        if( !handler )
            throw CommunicationError( "refusing to register an empty handler for action '" +
                                     action + "'" );
        // A specific, NON-EMPTY context is part of the envelope contract, but an
        // invalid context could never be targeted by any valid envelope
        // (contexts must be namespaced) - reject it here. An absent OR
        // explicitly empty context is the wildcard (round 7: contract and
        // implementation agree on "" == wildcard).
        if( context.has_value() && !context->empty() && !world::isNamespacedId( *context ) )
            throw CommunicationError( "handler context '" + *context +
                                     "' must be namespaced as <namespace>:<name>" );
        // Same rule for the M03 capability dimension: a specific capability
        // must be namespaced, absent/empty is the wildcard.
        if( capability.has_value() && !capability->empty() && !world::isNamespacedId( *capability ) )
            throw CommunicationError( "handler capability '" + *capability +
                                     "' must be namespaced as <namespace>:<name>" );

        RouteKey key;
        key.action = action;
        key.kind = kind;
        key.receiver = receiver.value_or( std::string{} );
        key.context = context.value_or( std::string{} );
        key.capability = capability.value_or( std::string{} );
        if( mRoutes.find( key ) != mRoutes.end() )
            throw CommunicationError( "a handler for action '" + action +
                                     "' of this kind/receiver/context/capability is "
                                     "already registered" );
        mRoutes.emplace( key, std::move( handler ) );
    }

    bool CommunicationRouter::hasRoute(
        const std::string &action, EnvelopeKind kind,
        const std::optional<std::string> &receiver,
        const std::optional<std::string> &context,
        const std::optional<std::string> &capability ) const
    {
        // Pure read: lets CommunicationRuntime validate a registration BEFORE
        // mutating any registry (transactional registration, M03 Round 1).
        RouteKey probe;
        probe.action = action;
        probe.kind = kind;
        probe.receiver = receiver.value_or( std::string{} );
        probe.context = context.value_or( std::string{} );
        probe.capability = capability.value_or( std::string{} );
        return mRoutes.find( probe ) != mRoutes.end();
    }

    CommunicationRouter::DispatchResult
    CommunicationRouter::dispatch( const CommunicationEnvelope &envelope ) const
    {
        validateEnvelope( envelope );

        // Match priority (deterministic, M02/M03):
        //  exact receiver+context -> receiver-only -> context-only -> wildcard,
        //  with an exact capability preferred over the capability wildcard
        //  inside every tier. An absent envelope capability is the wildcard
        //  (M02 envelopes keep routing exactly as before).
        const auto find = [&]( const std::string &receiver, const std::string &context,
                               const std::string &capability ) {
            RouteKey probe;
            probe.action = envelope.action;
            probe.kind = envelope.kind;
            probe.receiver = receiver;
            probe.context = context;
            probe.capability = capability;
            return mRoutes.find( probe );
        };

        DispatchResult result;
        auto it = mRoutes.end();
        const std::string receiver = envelope.receiver;
        const std::string context = envelope.context;
        const std::string cap = envelope.capability.value_or( std::string{} );
        if( it == mRoutes.end() ) it = find( receiver, context, cap );
        if( it == mRoutes.end() ) it = find( receiver, context, "" );
        if( it == mRoutes.end() ) it = find( receiver, "", cap );
        if( it == mRoutes.end() ) it = find( receiver, "", "" );
        if( it == mRoutes.end() ) it = find( "", context, cap );
        if( it == mRoutes.end() ) it = find( "", context, "" );
        if( it == mRoutes.end() ) it = find( "", "", cap );
        if( it == mRoutes.end() ) it = find( "", "", "" );
        if( it == mRoutes.end() )
            throw CommunicationError( "no handler for action '" + envelope.action +
                                     "' kind '" + std::to_string( static_cast<int>( envelope.kind ) ) +
                                     "' at receiver '" + envelope.receiver +
                                     "' context '" + envelope.context + "'" );
        // M03 trace: record the dispatched envelope, then the produced
        // replies - in order, read-only.
        if( mTrace )
            mTrace( envelope );
        it->second( envelope, result.replies );
        // M03 Round 1 (MAJOR 4 + Event contract): the bus boundary holds in
        // BOTH directions. An Event is strictly fire-and-forget - a handler
        // that fabricates replies violates the contract and is rejected
        // loudly - and every handler output must itself be a valid envelope
        // before it may leave the router.
        if( envelope.kind == EnvelopeKind::Event && !result.replies.empty() )
            throw CommunicationError( "an Event is fire-and-forget; handler for action '" +
                                     envelope.action + "' produced replies" );
        for( const auto &output : result.replies )
            validateEnvelope( output, "router output" );
        if( mTrace )
        {
            for( const auto &reply : result.replies )
                mTrace( reply );
        }
        result.handled = true;
        return result;
    }
} // namespace world::communication