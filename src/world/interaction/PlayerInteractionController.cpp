#include "world/interaction/PlayerInteractionController.h"

#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"

namespace world::interaction
{
    PlayerInteractionController::PlayerInteractionController(
        const ChunkManager &chunks, communication::MessageIdSource &ids,
        communication::CommunicationRouter &router ) :
        mChunks( chunks ),
        mIds( &ids ),
        mRouter( &router )
    {
    }

    PlayerInteractionController::PlayerInteractionController(
        const ChunkManager &chunks, communication::CommunicationRuntime &bus ) :
        mChunks( chunks ),
        mBus( &bus )
    {
        // The production path owns ONLY the runtime: ids come from its single
        // source, execution goes through its synchronous dispatch() - there
        // is deliberately no raw-router dispatch bypass (M03 Round 1).
    }

    PlayerInteractionController::ClickOutcome PlayerInteractionController::dispatchAction(
        const std::string &action, const BlockAddress &target,
        communication::Payload payload ) const
    {
        ClickOutcome outcome;
        communication::CommunicationEnvelope envelope;
        envelope.messageId = mBus ? mBus->nextMessageId() : mIds->next();
        envelope.kind = communication::EnvelopeKind::Command;
        envelope.sender = "player:1";
        envelope.receiver = "world:state";
        envelope.context = "core:world";
        envelope.action = action;
        envelope.target = WorldStateTarget( target );
        envelope.payload = std::move( payload );
        // Callback-free transport: an invalid command stays an error reply
        // from the bus/handlers; the host logs it. The PRODUCTION path uses
        // the runtime's SYNCHRONOUS dispatch() convenience (validation +
        // router execution, outputs in the DispatchResult; the A/B queues are
        // reserved for async producers like the later timer worker). The
        // legacy path keeps the M02 router contract.
        auto result =
            mBus ? mBus->dispatch( envelope ) : mRouter->dispatch( envelope );
        outcome.dispatched = result.handled;
        outcome.replies = std::move( result.replies );
        return outcome;
    }

    PlayerInteractionController::ClickOutcome PlayerInteractionController::pressPrimary(
        const WorldPosition &origin, double dirX, double dirY, double dirZ,
        double maxDistance )
    {
        ClickOutcome outcome;
        const auto hit = pickBlock( mChunks, origin, dirX, dirY, dirZ, maxDistance );
        outcome.picked = hit.has_value();
        outcome.hit = hit;
        if( !hit || !hit->adjacent )
            return outcome;
        // place exactly ONE canonical cell: the adjacent one (camera side of
        // the hit face); the payload can never redirect the target.
        const auto dispatched =
            dispatchAction( communication::ACTION_BLOCK_PLACE, *hit->adjacent,
                            communication::BlockPlacePayload{ mSelectedRuntimeId } );
        outcome.dispatched = dispatched.dispatched;
        outcome.replies = dispatched.replies;
        return outcome;
    }

    PlayerInteractionController::ClickOutcome PlayerInteractionController::pressSecondary(
        const WorldPosition &origin, double dirX, double dirY, double dirZ,
        double maxDistance )
    {
        ClickOutcome outcome;
        const auto hit = pickBlock( mChunks, origin, dirX, dirY, dirZ, maxDistance );
        outcome.picked = hit.has_value();
        outcome.hit = hit;
        if( !hit )
            return outcome;
        // remove targets exactly the picked block (air runtime id 0).
        const auto dispatched =
            dispatchAction( communication::ACTION_BLOCK_REMOVE, hit->block,
                            communication::Payload( std::monostate{} ) );
        outcome.dispatched = dispatched.dispatched;
        outcome.replies = dispatched.replies;
        return outcome;
    }
} // namespace world::interaction