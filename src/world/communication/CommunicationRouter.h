#pragma once

#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationError.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace world::communication
{
    /**
     * Message router (M02-B, #18; M03): dispatches a validated
     * CommunicationEnvelope to the handler registered for its action, kind,
     * receiver, context and capability.
     *
     * The router is the EXECUTION / ROUTING mechanism of the communication
     * bus - nothing more: it matches a validated envelope deterministically
     * and runs the bound handler. The semantic Signal/Slot/Action registries
     * live in CommunicationRegistries.h and are owned by
     * CommunicationRuntime; the route map of this class is the internal
     * binding/execution table the runtime builds from those registries (an
     * allowed internal optimization, never the contract).
     *
     * Routing dimensions (M02/M03):
     *  - action/kind are mandatory and validated at registration
     *    (namespaced action, valid EnvelopeKind);
     *  - receiver/context/capability are optional dimensions: "" (or absent)
     *    registers a wildcard that serves every receiver/context/capability;
     *  - dispatch matches: exact receiver+context, then receiver-only,
     *    then context-only, then wildcard - deterministically in this
     *    priority order, with an exact capability preferred over the
     *    capability wildcard inside every tier. Duplicate exact routes are
     *    rejected loudly.
     *
     * There is deliberately no second `signal -> vector<callbacks>` event
     * object: fan-out stays an internal optimization, never the contract.
     *
     * M03 generalization: the same transportable envelope drives Command,
     * Event, Query and Reply. An Event routes to its addressed handler; a
     * Query handler answers with a correlated Reply (makeReply); a Reply
     * always correlates with the concrete message it answers. The runtime
     * stages envelopes through its BoundedEnvelopeQueues; an optional trace
     * sink records every dispatched envelope and produced reply for the bus
     * trace. Handler outputs are validated at this boundary (MAJOR 4), and
     * Event handlers may never produce replies (fire-and-forget).
     */
    class CommunicationRouter
    {
    public:
        using Handler = std::function<void( const CommunicationEnvelope &,
                                            std::vector<CommunicationEnvelope> &replies )>;

        struct DispatchResult
        {
            bool handled = false;
            std::vector<CommunicationEnvelope> replies;
        };

        /** Registers a handler. A duplicate (action, kind, receiver, context,
         *  capability) route throws CommunicationError; the action and any
         *  explicit capability must be namespaced and the kind a real
         *  EnvelopeKind. Empty receiver/context/capability = wildcard. */
        void registerHandler( const std::string &action, EnvelopeKind kind, Handler handler,
                              const std::optional<std::string> &receiver = std::nullopt,
                              const std::optional<std::string> &context = std::nullopt,
                              const std::optional<std::string> &capability = std::nullopt );

        /** Non-mutating route presence check used by CommunicationRuntime to
         *  make registrations transactional (validation before mutation). */
        bool hasRoute( const std::string &action, EnvelopeKind kind,
                       const std::optional<std::string> &receiver = std::nullopt,
                       const std::optional<std::string> &context = std::nullopt,
                       const std::optional<std::string> &capability = std::nullopt ) const;

        /** Validates the envelope, matches (action, kind, receiver, context,
         *  capability) against the registered routes and runs the best
         *  matching handler. Throws CommunicationError for invalid envelopes,
         *  unmatched routes or unknown actions. Handler outputs are validated
         *  before they may leave the router. */
        DispatchResult dispatch( const CommunicationEnvelope &envelope ) const;

        /** Optional bus trace (M03): invoked for the dispatched envelope and
         *  for every produced reply, in order, after routing. The sink only
         *  reads - it can never mutate the bus. */
        void setTraceSink( std::function<void( const CommunicationEnvelope & )> sink )
        {
            mTrace = std::move( sink );
        }

        std::size_t routeCount() const { return mRoutes.size(); }

    private:
        // Route key: action | kind | receiver (""=wildcard) | context
        // (""=wildcard) | capability (""=wildcard); sorted for deterministic
        // lookup order.
        struct RouteKey
        {
            std::string action;
            EnvelopeKind kind = EnvelopeKind::Command;
            std::string receiver; // "" = any receiver
            std::string context;  // "" = any context
            std::string capability; // "" = any capability
            friend bool operator<( const RouteKey &l, const RouteKey &r )
            {
                if( l.action != r.action ) return l.action < r.action;
                if( static_cast<int>( l.kind ) != static_cast<int>( r.kind ) )
                    return static_cast<int>( l.kind ) < static_cast<int>( r.kind );
                if( l.receiver != r.receiver ) return l.receiver < r.receiver;
                if( l.context != r.context ) return l.context < r.context;
                return l.capability < r.capability;
            }
        };
        std::map<RouteKey, Handler> mRoutes;
        std::function<void( const CommunicationEnvelope & )> mTrace;
    };
} // namespace world::communication