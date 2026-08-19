#include "world/communication/BlockCommandHandlers.h"

#include "world/communication/CommunicationEnvelope.h"

namespace world::communication
{
    namespace
    {
        CommandResultPayload reject( const std::string &reason )
        {
            return CommandResultPayload{ false, reason, std::nullopt };
        }

        CommandResultPayload accept()
        {
            return CommandResultPayload{ true, {}, std::nullopt };
        }

        void replyFor( const CommunicationEnvelope &cause, MessageIdSource &ids,
                       CommandResultPayload result, std::vector<CommunicationEnvelope> &replies )
        {
            replies.push_back( makeReply( cause, std::move( result ), ids ) );
        }
    } // namespace

    void registerBlockCommandHandlers( CommunicationRouter &router, WorldState &worldState,
                                       MessageIdSource &messageIds )
    {
        // M02 round 6: the authoritative block commands are EXACTLY bound
        // to the world-state bus address - receiver "world:state",
        // context "core:world". An envelope addressed elsewhere
        // (inventory:system / inventory:context) never reaches these
        // handlers; the generic wildcard routes stay available for other
        // actions (M03 routing).
        router.registerHandler( ACTION_BLOCK_PLACE, EnvelopeKind::Command,
                                [&]( const CommunicationEnvelope &envelope,
                                     std::vector<CommunicationEnvelope> &replies ) {
            // The envelope's target IS authoritative (M02 review): a payload
            // can never name a different block than the one mutated.
            const auto *payload = std::get_if<BlockPlacePayload>( &envelope.payload );
            if( !payload )
                return replyFor( envelope, messageIds,
                                 reject( "payload must be a block place payload" ), replies );
            // Air (runtime id 0) placement would sneak a second removal path
            // into core:block.place (M02 review round 3): removals go
            // through core:block.remove exclusively.
            if( payload->runtimeId == 0u )
                return replyFor( envelope, messageIds,
                                 reject( "placing AIR is a removal, use 'core:block.remove'" ),
                                 replies );
            if( !envelope.target.has_value() || !envelope.target->isBlock() )
                return replyFor( envelope, messageIds,
                                 reject( "placement target must be a block address" ), replies );
            // Placement only happens in a LOADED, materialized world cell:
            // writing into an unloaded chunk would create an all-AIR chunk
            // ahead of deterministic worldgen (M02 review round 5) - that
            // chunk would then never receive its generated content.
            const std::optional<std::uint16_t> existing =
                worldState.blockAt( envelope.target->asBlock() );
            if( !existing.has_value() )
                return replyFor( envelope, messageIds,
                                 reject( "target chunk is not loaded" ), replies );
            // Placement must never silently overwrite an existing non-AIR
            // block: that would be an undeclared remove+place mutation
            // (M02 review round 4). Removals go through core:block.remove.
            if( *existing != 0u )
                return replyFor( envelope, messageIds,
                                 reject( "target cell is occupied" ), replies );
            const bool changed =
                worldState.setBlock( envelope.target->asBlock(), payload->runtimeId );
            replyFor( envelope, messageIds,
                      changed ? accept() : reject( "placement rejected" ), replies );
        },
            std::optional<std::string>{ "world:state" },
            std::optional<std::string>{ "core:world" } );

        router.registerHandler( ACTION_BLOCK_REMOVE, EnvelopeKind::Command,
                                [&]( const CommunicationEnvelope &envelope,
                                     std::vector<CommunicationEnvelope> &replies ) {
            // remove has no payload contract: an envelope carrying any other
            // payload type is a rejected command, never a mutation (M02
            // review round 3: wrong typed payload must not mutate).
            if( !std::holds_alternative<std::monostate>( envelope.payload ) )
                return replyFor( envelope, messageIds,
                                 reject( "remove requires an empty payload" ), replies );
            if( !envelope.target.has_value() || !envelope.target->isBlock() )
                return replyFor( envelope, messageIds,
                                 reject( "remove target must be a block address" ), replies );
            // Removal also only applies to loaded cells (an unloaded chunk
            // holds nothing to remove; AIR no-ops never materialize).
            const std::optional<std::uint16_t> existing =
                worldState.blockAt( envelope.target->asBlock() );
            if( !existing.has_value() )
                return replyFor( envelope, messageIds,
                                 reject( "target chunk is not loaded" ), replies );
            const bool changed = worldState.setBlock( envelope.target->asBlock(), 0u ); // AIR
            replyFor( envelope, messageIds,
                      changed ? accept() : reject( "removal rejected" ), replies );
        },
            std::optional<std::string>{ "world:state" },
            std::optional<std::string>{ "core:world" } );
    }

    void registerBlockCommandHandlers( CommunicationRuntime &bus, WorldState &worldState )
    {
        // M03 Round 1: the runtime binds the exact same commands with the
        // same semantics (receiver "world:state", context "core:world"),
        // declaring their payload schemas. Message ids and replies flow
        // through the runtime's single id source; the player/input path uses
        // its SYNCHRONOUS dispatch() (outputs in the DispatchResult), while
        // the bounded A/B queues serve the async producer path (Round 2).
        bus.registerHandler( ACTION_BLOCK_PLACE, EnvelopeKind::Command,
                             [&bus, &worldState]( const CommunicationEnvelope &envelope,
                                                  std::vector<CommunicationEnvelope> &replies ) {
            const auto *payload = std::get_if<BlockPlacePayload>( &envelope.payload );
            if( !payload )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "payload must be a block place payload" ) ) );
            if( payload->runtimeId == 0u )
                return replies.push_back(
                    bus.makeReply( envelope,
                                   reject( "placing AIR is a removal, use 'core:block.remove'" ) ) );
            if( !envelope.target.has_value() || !envelope.target->isBlock() )
                return replies.push_back(
                    bus.makeReply( envelope,
                                   reject( "placement target must be a block address" ) ) );
            const std::optional<std::uint16_t> existing =
                worldState.blockAt( envelope.target->asBlock() );
            if( !existing.has_value() )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "target chunk is not loaded" ) ) );
            if( *existing != 0u )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "target cell is occupied" ) ) );
            const bool changed =
                worldState.setBlock( envelope.target->asBlock(), payload->runtimeId );
            replies.push_back( bus.makeReply( envelope,
                                              changed ? accept() : reject( "placement rejected" ) ) );
        },
            std::optional<std::string>{ "world:state" },
            std::optional<std::string>{ "core:world" },
            std::nullopt, PayloadSchema::BlockPlace );

        bus.registerHandler( ACTION_BLOCK_REMOVE, EnvelopeKind::Command,
                             [&bus, &worldState]( const CommunicationEnvelope &envelope,
                                                  std::vector<CommunicationEnvelope> &replies ) {
            if( !std::holds_alternative<std::monostate>( envelope.payload ) )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "remove requires an empty payload" ) ) );
            if( !envelope.target.has_value() || !envelope.target->isBlock() )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "remove target must be a block address" ) ) );
            const std::optional<std::uint16_t> existing =
                worldState.blockAt( envelope.target->asBlock() );
            if( !existing.has_value() )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "target chunk is not loaded" ) ) );
            const bool changed = worldState.setBlock( envelope.target->asBlock(), 0u );
            replies.push_back( bus.makeReply( envelope,
                                              changed ? accept() : reject( "removal rejected" ) ) );
        },
            std::optional<std::string>{ "world:state" },
            std::optional<std::string>{ "core:world" },
            std::nullopt, PayloadSchema::None );

        // Generic registered-property mutation. The payload owns the
        // property id and typed value; C++ contains no content/gameplay
        // property names. WorldState remains the sole mutation authority and
        // validates target scope, capability, value type and bit width.
        bus.registerHandler( ACTION_PROPERTY_SET, EnvelopeKind::Command,
                             [&bus, &worldState]( const CommunicationEnvelope &envelope,
                                                  std::vector<CommunicationEnvelope> &replies ) {
            const auto *payload = std::get_if<PropertySetPayload>( &envelope.payload );
            if( !payload )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "payload must be a property set payload" ) ) );
            if( !envelope.target.has_value() )
                return replies.push_back(
                    bus.makeReply( envelope, reject( "property target must be present" ) ) );
            const bool changed =
                worldState.set( *envelope.target, payload->property, payload->value );
            replies.push_back( bus.makeReply(
                envelope, changed ? accept() : reject( "property write rejected" ) ) );
        },
            std::optional<std::string>{ "world:state" },
            std::optional<std::string>{ "core:world" },
            std::nullopt, PayloadSchema::PropertySet );
    }
} // namespace world::communication
