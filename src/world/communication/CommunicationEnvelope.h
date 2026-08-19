#pragma once

#include "world/communication/CommunicationError.h"
#include "world/registry/Registry.h"
#include "world/state/WorldStateTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace world::communication
{
    /** Message family of a CommunicationEnvelope (M02, issue #18). */
    enum class EnvelopeKind : std::uint8_t
    {
        Command, // request an authoritative mutation/action
        Query,   // read-only request (M03)
        Event,   // broadcast fact (M03)
        Reply    // correlated answer to a Command/Query
    };

    /** Player intent: place the block payload.runtimeId at the canonical
     *  target (the envelope's target IS the placement address; M02 review:
     *  the payload carries no competing block address). */
    struct BlockPlacePayload
    {
        std::uint16_t runtimeId = 0;
    };

    /** Typed block-address payload. This is deliberately distinct from the
     *  envelope target: a message may act on one block while carrying another
     *  canonical block reference as data (for example a peer selected by
     *  content). `replyTo` must never be repurposed for that data. */
    struct BlockTargetPayload
    {
        BlockAddress target;
    };

    /** Generic authoritative property-write request. Content supplies the
     *  registered property id and typed value; production C++ owns only the
     *  WorldState mutation mechanism and knows no gameplay property names. */
    struct PropertySetPayload
    {
        std::string property;
        PropertyValue value;
    };
    // remove intent needs no payload: the envelope's target is the only
    // address the authoritative handler may mutate (M02 review).

    /** Typed command result carried by a Reply (ok + failure text). */
    struct CommandResultPayload
    {
        bool ok = false;
        std::string error;
        std::optional<PropertyValue> value; // optional result the caller asked for
    };

    /** Typed query request (M03): names the property the receiver must
     *  answer; the answer travels back as a correlated Reply whose
     *  CommandResultPayload carries the value. */
    struct QueryPayload
    {
        std::string property;
    };

    /** Typed event payload (M03): one transportable property value. Never a
     *  pointer or Lua reference. */
    struct EventValuePayload
    {
        PropertyValue value;
    };

    /** Typed transport payload (M02-A/M03). Transportable by value and
     *  copyable; never holds pointers or Lua references - the permanent
     *  contract. */
    using Payload = std::variant<std::monostate, BlockPlacePayload, BlockTargetPayload,
                                 PropertySetPayload, CommandResultPayload, QueryPayload,
                                 EventValuePayload>;

    /**
     * The permanent message contract (docs/ARCHITECTURE.md,
     * M02/M03; #15). One logical contract that later works through direct
     * dispatch, bounded queues, worker queues, loopback and network - no
     * second event object ever.
     *
     * Fields:
     *  - messageId: unique (MessageIdSource), > 0;
     *  - kind: Command | Query | Event | Reply;
     *  - sender/receiver: logical ids ("player:1", "world:state", "block:B");
     *  - context/action: namespaced contracts, e.g. "core:world" /
     *    "core:block.place";
     *  - capability (M03): optional namespaced capability a capability-routed
     *    handler requires; absent = no capability constraint;
     *  - target: canonical WorldStateTarget (Block..Sector) - reused from
     *    M01-B; std::optional so an envelope can exist WITHOUT inventing a
     *    fake origin target (never a default-constructed block);
     *  - payload: typed variant (no void*, no pointers, no lua refs);
     *  - replyTo: LOGICAL REPLY ADDRESS (a transportable address like
     *    "player:1"), never a message id (M02 review);
     *  - correlationId: numeric handle of the message this envelope
     *    follows/serves (Reply/Folgekommunikation).
     */
    struct CommunicationEnvelope
    {
        std::uint64_t messageId = 0;
        EnvelopeKind kind = EnvelopeKind::Command;
        std::string sender;
        std::string receiver;
        std::string context;
        std::string action;
        std::optional<std::string> capability; // M03: namespaced capability requirement
        std::optional<WorldStateTarget> target; // absent = intent without a world location
        Payload payload;
        /** Logical reply address (e.g. "player:1"); never a message id. */
        std::optional<std::string> replyTo;
        std::optional<std::uint64_t> correlationId;
    };

    /** Strict structural validation (permanent contract). Throws
     *  CommunicationError listing the offending field. */
    inline void validateEnvelope( const CommunicationEnvelope &envelope,
                                  const std::string &where = "envelope" )
    {
        if( envelope.messageId == 0u )
            throw CommunicationError( where + ": 'messageId' must be > 0" );
        // M02 review round 3: out-of-range programmatic enum values are as
        // invalid as unknown scope/valueType/storage worked in the sidecar
        // registry - a static_cast can never smuggle a foreign kind through.
        switch( envelope.kind )
        {
            case EnvelopeKind::Command:
            case EnvelopeKind::Query:
            case EnvelopeKind::Event:
            case EnvelopeKind::Reply:
                break;
            default:
                throw CommunicationError( where + ": invalid EnvelopeKind value" );
        }
        if( envelope.sender.empty() )
            throw CommunicationError( where + ": 'sender' must not be empty" );
        if( envelope.receiver.empty() )
            throw CommunicationError( where + ": 'receiver' must not be empty" );
        if( !world::isNamespacedId( envelope.context ) )
            throw CommunicationError( where + ": 'context' must be namespaced as "
                                     "<namespace>:<name>" );
        if( !world::isNamespacedId( envelope.action ) )
            throw CommunicationError( where + ": 'action' must be namespaced as "
                                     "<namespace>:<name>" );
        // M03: an explicit capability must be namespaced - a broken capability
        // could never be required by a capability-routed handler.
        if( envelope.capability.has_value() &&
            ( envelope.capability->empty() || !world::isNamespacedId( *envelope.capability ) ) )
            throw CommunicationError( where + ": 'capability' must be a non-empty "
                                     "namespaced id" );
        if( envelope.kind == EnvelopeKind::Reply )
        {
            // A Reply must follow a concrete message (numeric correlation)
            // and name its logical reply address.
            if( !envelope.correlationId.has_value() || *envelope.correlationId == 0u )
                throw CommunicationError( where +
                                         ": a Reply must carry a correlationId > 0" );
            if( !envelope.replyTo.has_value() || envelope.replyTo->empty() )
                throw CommunicationError( where +
                                         ": a Reply must carry a non-empty 'replyTo' "
                                         "logical address" );
        }
        // An Event may OPTIONALLY carry a correlationId - never as a reply
        // chain, but as causal follow-up communication (e.g. a timer message
        // 100 causing an Event 101 with correlationId = 100). The generic
        // "> 0 when set" rule below still applies.
        // correlationId == 0 is reserved: a present correlation must always
        // name a real message (M02 review round 5).
        if( envelope.correlationId.has_value() && *envelope.correlationId == 0u )
            throw CommunicationError( where + ": 'correlationId' must be > 0 when set" );
    }

    /**
     * Sequential message id source (>= 1; not persisted in M02). M03 Round 1
     * (MAJOR 1): a MessageIdSource is the unique id space of ONE
     * communication runtime context. It is deliberately NON-copyable and
     * non-movable so a copied/forked source can never silently spawn a second
     * colliding sequence; CommunicationRuntime owns the single source of its
     * context and every producer draws its ids from it. Overflow is guarded:
     * the >0 invariant never wraps.
     */
    class MessageIdSource
    {
    public:
        MessageIdSource() = default;
        MessageIdSource( const MessageIdSource & ) = delete;
        MessageIdSource &operator=( const MessageIdSource & ) = delete;
        MessageIdSource( MessageIdSource && ) = delete;
        MessageIdSource &operator=( MessageIdSource && ) = delete;

        std::uint64_t next();

    private:
        std::uint64_t mCurrent = 0;
    };

    /** Builds the correlated Reply for `cause` (kind=Reply; the reply is
     *  addressed to the cause's explicit non-empty replyTo, else to the
     *  cause's sender; correlationId ALWAYS equals cause.messageId - a
     *  stale chain id on the cause is never propagated). Callbacks are
     *  never envelope fields - replies are the transportable answer. */
    CommunicationEnvelope makeReply( const CommunicationEnvelope &cause,
                                     CommandResultPayload result,
                                     MessageIdSource &ids );
} // namespace world::communication
