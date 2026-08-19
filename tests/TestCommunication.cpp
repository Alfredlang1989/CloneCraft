#include "TestHarness.h"
#include "world/communication/CommunicationEnvelope.h"

#include <functional>
#include <string>

namespace
{
    bool rejected( const std::function<void()> &fn )
    {
        try
        {
            fn();
        }
        catch( const world::communication::CommunicationError & )
        {
            return true;
        }
        return false;
    }

    world::communication::CommunicationEnvelope validCommand( std::uint64_t id )
    {
        world::communication::CommunicationEnvelope env;
        env.messageId = id;
        env.kind = world::communication::EnvelopeKind::Command;
        env.sender = "player:1";
        env.receiver = "world:state";
        env.context = "core:world";
        env.action = "core:block.place";
        env.target = world::BlockAddress{};
        env.payload = world::communication::BlockPlacePayload{ 1u };
        return env;
    }
} // namespace

TEST_CASE( envelope_validation_accepts_a_valid_command )
{
    const auto env = validCommand( 42 );
    CHECK( !rejected( [&] { world::communication::validateEnvelope( env ); } ) );
}

TEST_CASE( envelope_validation_rejects_broken_fields )
{
    // messageId 0 is never a valid sent message.
    CHECK( rejected( [&] { world::communication::validateEnvelope( validCommand( 0u ) ); } ) );
    auto broken = validCommand( 1 );
    broken.sender.clear();
    CHECK( rejected( [&] { world::communication::validateEnvelope( broken ); } ) );
    broken = validCommand( 1 );
    broken.receiver.clear();
    CHECK( rejected( [&] { world::communication::validateEnvelope( broken ); } ) );
    broken = validCommand( 1 );
    broken.context = "nocolon";
    CHECK( rejected( [&] { world::communication::validateEnvelope( broken ); } ) );
    broken = validCommand( 1 );
    broken.action = "core/block/place";
    CHECK( rejected( [&] { world::communication::validateEnvelope( broken ); } ) );
    broken = validCommand( 1 );
    broken.kind = world::communication::EnvelopeKind::Reply; // no reply fields
    CHECK( rejected( [&] { world::communication::validateEnvelope( broken ); } ) );
}

TEST_CASE( envelope_target_is_optional_and_never_fakes_origin )
{
    // M02-A inherits the M01-B rule: target is std::optional, so an envelope
    // without a world location exists honestly - it never silently points
    // at the origin block.
    auto env = validCommand( 1 );
    env.target.reset();
    CHECK( !env.target.has_value() );
    const auto withTarget = validCommand( 1 );
    CHECK( withTarget.target.has_value() );
    CHECK( withTarget.target->isBlock() );
}

TEST_CASE( envelope_payload_is_typed_and_transportable )
{
    // The payload is a typed variant - no void*, no pointers, no Lua refs.
    const auto env = validCommand( 1 );
    const auto *place = std::get_if<world::communication::BlockPlacePayload>( &env.payload );
    CHECK( place != nullptr );
    if( place )
        CHECK_EQ( place->runtimeId, 1u );

    world::communication::CommunicationEnvelope remove;
    remove.messageId = 2;
    remove.kind = world::communication::EnvelopeKind::Command;
    remove.sender = "player:1";
    remove.receiver = "world:state";
    remove.context = "core:world";
    remove.action = "core:block.remove";
    remove.target = world::BlockAddress{};
    remove.payload = std::monostate{}; // removal needs no payload: target is authoritative
    CHECK( std::holds_alternative<std::monostate>( remove.payload ) );
    world::communication::validateEnvelope( remove );
}

TEST_CASE( envelope_reply_correlates_cleanly )
{
    world::communication::MessageIdSource ids;
    const auto cause = validCommand( ids.next() );
    const auto reply = world::communication::makeReply(
        cause, world::communication::CommandResultPayload{ false, "target not placeable", std::nullopt },
        ids );

    CHECK( reply.kind == world::communication::EnvelopeKind::Reply );
    CHECK( reply.messageId != cause.messageId );
    CHECK( reply.sender == cause.receiver );
    CHECK( reply.receiver == cause.sender );
    CHECK( reply.context == cause.context );
    CHECK( reply.action == cause.action );
    CHECK( reply.target == cause.target );
    CHECK( reply.replyTo == cause.receiver ); // logical return address
    CHECK( reply.correlationId.has_value() );
    const auto *result = std::get_if<world::communication::CommandResultPayload>( &reply.payload );
    CHECK( result != nullptr );
    if( result )
    {
        CHECK( !result->ok );
        CHECK( result->error == "target not placeable" );
    }
    CHECK( !rejected( [&] { world::communication::validateEnvelope( reply ); } ) );
}

TEST_CASE( envelope_messages_are_unique_via_id_source )
{
    world::communication::MessageIdSource ids;
    CHECK_EQ( ids.next(), std::uint64_t{ 1 } );
    CHECK_EQ( ids.next(), std::uint64_t{ 2 } );
}

TEST_CASE( envelope_reply_always_correlates_with_cause_message_id )
{
    // round-4 fix: the reply correlation is ALWAYS the id of the concrete
    // message it answers - never a stale chain id from the cause.
    world::communication::MessageIdSource ids;
    auto cause = validCommand( ids.next() );
    cause.correlationId = 999u; // stale chain id must not win
    const auto reply = world::communication::makeReply(
        cause,
        world::communication::CommandResultPayload{ true, {}, std::nullopt }, ids );
    CHECK( reply.correlationId == cause.messageId );
    CHECK( reply.correlationId != 999u );
    world::communication::validateEnvelope( reply );
}

TEST_CASE( envelope_reply_empty_reply_address_falls_back_to_sender )
{
    // round-4 fix: an empty request.replyTo must never produce an invalid
    // Reply after a mutation.
    world::communication::MessageIdSource ids;
    auto cause = validCommand( ids.next() );
    cause.replyTo = std::string{};
    const auto reply = world::communication::makeReply(
        cause,
        world::communication::CommandResultPayload{ false, "nope", std::nullopt }, ids );
    CHECK( reply.receiver == cause.sender ); // fallback, not empty
    world::communication::validateEnvelope( reply ); // valid by construction
}

TEST_CASE( envelope_zero_correlation_id_is_rejected )
{
    // Round 5: correlationId 0 is reserved - a present correlation always
    // names a real message.
    auto env = validCommand( 1 );
    env.correlationId = 0u;
    CHECK( rejected( [&] { world::communication::validateEnvelope( env ); } ) );
}

int main() { return test::runAll(); }