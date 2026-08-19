#pragma once

#include "world/communication/CommunicationRegistries.h"
#include "world/communication/CommunicationRouter.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/state/WorldState.h"

namespace world::communication
{
    /** Registered actions of the M02 block-mutation commands. */
    inline constexpr const char *ACTION_BLOCK_PLACE = "core:block.place";
    inline constexpr const char *ACTION_BLOCK_REMOVE = "core:block.remove";
    inline constexpr const char *ACTION_PROPERTY_SET = "core:property.set";

    /**
     * Registers the authoritative block mutation handlers on the router
     * (M02-B, #18): "core:block.place" and "core:block.remove".
     *
     * The player expresses intent through CommunicationEnvelopes here; the
     * authoritative world mutation stays in WorldState - inputs/actions
     * never touch ChunkManager directly. Every command produces a
     * correlated Reply (kind=Reply, replyTo/correlationId), accepted or
     * rejected.
     */
    void registerBlockCommandHandlers( CommunicationRouter &router, WorldState &worldState,
                                     MessageIdSource &messageIds );

    /**
     * M03 production runtime variant. Registers the block commands plus the
     * generic registered-property command on a CommunicationRuntime. The handler
     * gets its message ids from the runtime's single source and replies
     * through the same bus. The two overloads bind identical semantics, so
     * the M02 regression stays intact.
     *
     * Player/Input -> CommunicationRuntime::dispatch(): the synchronous
     * validated runtime path (validation + router execution, outputs in the
     * DispatchResult). The bounded A/B queues are the ASYNC producer path
     * (submit()/pump*()/nextOutput()), reserved for producers like the
     * Round-2 timer worker.
     */
    void registerBlockCommandHandlers( CommunicationRuntime &bus, WorldState &worldState );
} // namespace world::communication
