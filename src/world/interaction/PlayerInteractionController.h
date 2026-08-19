#pragma once

#include "world/communication/CommunicationRuntime.h"
#include "world/interaction/BlockPicker.h"
#include "world/state/WorldState.h"

#include <cstdint>
#include <optional>

namespace world::interaction
{
    /**
     * The player interaction path of M02-D (issue #18): the input->world
     * funnel that bundles picking (BlockPicker), intent (CommunicationEnvelope)
     * and authoritative dispatch (CommunicationRouter/WorldState).
     *
     * The controller is input-agnostic on purpose: the SDL/Ogre host builds a
     * WorldPosition + ray from a mouse click and calls pressLeft()/pressRight().
     * Input NEVER reaches ChunkManager - the only mutating path is
     * Command(Envelope) -> Runtime validation -> Router -> WorldState.
     *
     * M03 Round 1: the PRODUCTION controller owns ONLY a CommunicationRuntime
     * and uses its SYNCHRONOUS dispatch() convenience adapter (validation +
     * router execution, outputs in the DispatchResult - the A/B queues are
     * not involved on this gameplay path). The runtime deliberately exposes
     * no raw-router dispatch bypass. The legacy M02 constructor keeps the
     * explicit CommunicationRouter& for the M02 regression tests.
     */
    class PlayerInteractionController
    {
    public:
        /** M02 regression path: explicit router + caller-provided id source. */
        PlayerInteractionController( const ChunkManager &chunks,
                                     communication::MessageIdSource &ids,
                                     communication::CommunicationRouter &router );

        /** M03 production path: the runtime's single id source and its
         *  synchronous dispatch() convenience. */
        PlayerInteractionController( const ChunkManager &chunks,
                                     communication::CommunicationRuntime &bus );

        /** The runtime block id the next primary click places. */
        void setSelectedRuntimeId( std::uint16_t runtimeId ) { mSelectedRuntimeId = runtimeId; }
        std::uint16_t selectedRuntimeId() const { return mSelectedRuntimeId; }

        struct ClickOutcome
        {
            bool picked = false;        // a block was targeted by the raycast
            bool dispatched = false;    // the envelope reached a handler
            std::optional<BlockPickResult> hit;
            std::vector<communication::CommunicationEnvelope> replies;
        };

        /** Primary click: place the selected block on the hit face
         *  (adjacent canonical cell). */
        ClickOutcome pressPrimary( const WorldPosition &origin, double dirX, double dirY,
                                   double dirZ, double maxDistance );

        /** Secondary click: remove the targeted block. */
        ClickOutcome pressSecondary( const WorldPosition &origin, double dirX, double dirY,
                                     double dirZ, double maxDistance );

    private:
        ClickOutcome dispatchAction( const std::string &action, const BlockAddress &target,
                                     communication::Payload payload ) const;

        const ChunkManager &mChunks;
        communication::MessageIdSource *mIds = nullptr;
        const communication::CommunicationRouter *mRouter = nullptr;
        communication::CommunicationRuntime *mBus = nullptr;
        std::uint16_t mSelectedRuntimeId = 0;
    };
} // namespace world::interaction