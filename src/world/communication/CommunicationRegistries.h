#pragma once

#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRouter.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace world::communication
{
    /** Shared executable of a registered Action (native C++ today; Lua and
     *  property-mutation reactions arrive in later M03 rounds). */
    using Handler = CommunicationRouter::Handler;

    /**
     * Payload contract of a Signal (M03, typed payload schemas).
     *
     * A declared Signal owns the schema its envelopes must carry; a Slot
     * declares the schema it expects. Incompatible wiring (Signal A carries
     * schema X, Slot B expects Y) is rejected at registration/mod-load time,
     * never discovered later when a Lua handler receives a wrong payload.
     *
     * Enums are validated centrally (findDefinedSchema): only the defined
     * values below are accepted, anything else is a loud CommunicationError.
     */
    enum class PayloadSchema : std::uint8_t
    {
        Any,          // slot-only: accepts any payload; never declared on a signal
        None,         // std::monostate (e.g. core:block.remove)
        BlockPlace,   // BlockPlacePayload
        BlockTarget,  // BlockTargetPayload
        PropertySet,  // PropertySetPayload
        CommandResult,// CommandResultPayload (replies)
        Query,        // QueryPayload
        EventValue    // EventValuePayload
    };

    /** True iff `schema` is one of the DEFINED enum values (Any included). */
    bool isValidPayloadSchema( PayloadSchema schema );

    /**
     * Central PayloadSchema gate: throws CommunicationError for unknown enum
     * values (e.g. a forged static_cast<PayloadSchema>(99)) and - when
     * `allowAny` is false - for PayloadSchema::Any (a signal must declare a
     * concrete contract; Any is a slot-side only acceptance).
     */
    void requirePayloadSchema( PayloadSchema schema, bool allowAny, const char *what );

    /** A signal must always declare a concrete payload contract. */
    bool isConcreteSchema( PayloadSchema schema );
    const char *schemaName( PayloadSchema schema );
    /** Registration-time compatibility: does a slot expecting `slotSchema`
     *  accept a signal declared with `signalSchema`? "Any" accepts everything;
     *  otherwise both must be identical. Callers validate both values first
     *  (requirePayloadSchema). */
    bool schemaAccepts( PayloadSchema signalSchema, PayloadSchema slotSchema );
    /** Dispatch-time check: does the envelope actually carry the declared
     *  schema? Applied to every inbound envelope at the bus boundary. */
    bool payloadMatches( const Payload &payload, PayloadSchema schema );

    /** True iff `kind` is one of the defined EnvelopeKind values. */
    bool isValidEnvelopeKind( EnvelopeKind kind );

    /**
     * Declared handler output contract of a Slot/Binding (M03 Round 1,
     * MAJOR 1+3).
     *
     * The runtime validates every handler output against this contract AFTER
     * execution and pre-flights its capacity BEFORE execution. A handler may
     * produce FEWER than maxOutputs outputs, never more.
     *
     * Round-1 semantics:
     *  - fire-and-forget (Event / no-output slots): maxOutputs = 0;
     *  - reply-producing handlers (block commands, query handlers):
     *    maxOutputs = 1, kind = Reply, schema = CommandResult,
     *    requireCorrelation = true (correlationId == cause.messageId).
     */
    struct OutputContract
    {
        std::size_t maxOutputs = 0;
        EnvelopeKind kind = EnvelopeKind::Reply;
        PayloadSchema schema = PayloadSchema::CommandResult;
        bool requireCorrelation = false;
    };

    /** Kind-based default contract (Command/Query -> one correlated Reply;
     *  Event/Reply -> fire-and-forget). Registrations may override it
     *  explicitly; later output patterns extend the contract, not this
     *  milestone. */
    inline OutputContract defaultOutputContract( EnvelopeKind kind )
    {
        switch( kind )
        {
            case EnvelopeKind::Command:
            case EnvelopeKind::Query:
                return OutputContract{ 1, EnvelopeKind::Reply,
                                       PayloadSchema::CommandResult, true };
            case EnvelopeKind::Event:
            case EnvelopeKind::Reply:
                return OutputContract{ 0, EnvelopeKind::Reply,
                                       PayloadSchema::CommandResult, false };
        }
        return OutputContract{ 0, EnvelopeKind::Reply, PayloadSchema::CommandResult, false };
    }

    /**
     * Signal Registry (M03): a Signal is the namespaced semantic
     * event/request - "something happened / is requested" - keyed by the
     * (action, kind) the envelope carries and owning the payload schema of
     * that signal. This is the declaration contract, not an executable.
     */
    class SignalRegistry
    {
    public:
        struct Key
        {
            std::string action;
            EnvelopeKind kind = EnvelopeKind::Command;
            friend bool operator<( const Key &l, const Key &r )
            {
                if( l.action != r.action ) return l.action < r.action;
                return static_cast<int>( l.kind ) < static_cast<int>( r.kind );
            }
        };
        struct Definition
        {
            PayloadSchema schema = PayloadSchema::Any;
        };

        /** Declares (action, kind) with a concrete payload schema. A
         *  duplicate declaration with a DIFFERENT schema is a wiring conflict
         *  and rejected loudly; re-declaring the identical schema is
         *  idempotent (a signal serves any number of slots). */
        void declare( const std::string &action, EnvelopeKind kind, PayloadSchema schema );

        /** Pure validation of a declaration (throws, changes NOTHING): the
         *  single source of truth the runtime's transactional registration
         *  uses before it commits. */
        void checkCanDeclare( const std::string &action, EnvelopeKind kind,
                              PayloadSchema schema ) const;

        bool contains( const std::string &action, EnvelopeKind kind ) const;
        PayloadSchema schemaOf( const std::string &action, EnvelopeKind kind ) const;
        std::size_t size() const { return mSignals.size(); }

        /** Internal rollback hook for the runtime's transactional
         *  registration; not part of the binding contract. */
        void erase( const std::string &action, EnvelopeKind kind );

    private:
        std::map<Key, Definition> mSignals;
    };

    /**
     * Slot Registry (M03): a Slot is "this receiver/context/capability reacts
     * to signal X". It carries the payload schema the slot expects and the
     * Action it binds to (the reaction). Routing selects a slot; binding
     * resolves the action id. The router's route map stays the low-level
     * execution table - this registry is the semantic slot contract.
     */
    class SlotRegistry
    {
    public:
        struct Key
        {
            std::string action;
            EnvelopeKind kind = EnvelopeKind::Command;
            std::string receiver;   // "" = any receiver (wildcard)
            std::string context;    // "" = any context (wildcard)
            std::string capability; // "" = no capability constraint
            friend bool operator<( const Key &l, const Key &r )
            {
                if( l.action != r.action ) return l.action < r.action;
                if( static_cast<int>( l.kind ) != static_cast<int>( r.kind ) )
                    return static_cast<int>( l.kind ) < static_cast<int>( r.kind );
                if( l.receiver != r.receiver ) return l.receiver < r.receiver;
                if( l.context != r.context ) return l.context < r.context;
                return l.capability < r.capability;
            }
        };
        struct Definition
        {
            std::string action;
            EnvelopeKind kind = EnvelopeKind::Command;
            std::string receiver;   // "" = any
            std::string context;    // "" = any
            std::string capability; // "" = none
            PayloadSchema expected = PayloadSchema::Any;
            std::string actionId;   // bound Action (ActionRegistry)
            OutputContract contract; // declared output contract of this binding
        };

        /** Inserts a slot; an exact duplicate (same action/kind/receiver/
         *  context/capability) is rejected loudly. */
        void insert( const Definition &definition );

        bool contains( const std::string &action, EnvelopeKind kind,
                       const std::string &receiver, const std::string &context,
                       const std::string &capability ) const;

        /** Best-match slot using the deterministic priority order of the
         *  router: exact receiver+context -> receiver-only -> context-only ->
         *  wildcard, with an exact capability preferred over the capability
         *  wildcard inside every tier. Returns nullptr when no slot matches. */
        const Definition *find( const std::string &action, EnvelopeKind kind,
                                const std::string &receiver, const std::string &context,
                                const std::string &capability ) const;

        /** Internal rollback only for the runtime's transactional
         *  registration; not part of the binding contract. */
        void erase( const std::string &action, EnvelopeKind kind,
                    const std::string &receiver, const std::string &context,
                    const std::string &capability );

        std::size_t size() const { return mSlots.size(); }

    private:
        std::map<Key, Definition> mSlots;
    };

    /**
     * Action Registry (M03): a registered, executable reaction, keyed by a
     * namespaced action id. Today the reaction is a native C++ Handler;
     * later M03 rounds add Lua handlers, property mutations and signal
     * emissions as further action implementations. Slots bind to actions.
     */
    class ActionRegistry
    {
    public:
        /** Registers an executable; a duplicate action id is rejected
         *  loudly. The id is validated as namespaced by callers (the
         *  explicit CommunicationRuntime::registerAction enforces it). */
        void insert( const std::string &actionId, Handler handler );
        const Handler *find( const std::string &actionId ) const;

        /** Internal rollback only for the runtime's transactional
         *  registration; not part of the binding contract. */
        void erase( const std::string &actionId );

        std::size_t size() const { return mActions.size(); }

    private:
        std::map<std::string, Handler> mActions;
    };
} // namespace world::communication
