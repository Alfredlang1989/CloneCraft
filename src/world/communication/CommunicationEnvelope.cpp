#include "world/communication/CommunicationEnvelope.h"

#include "world/communication/CommunicationError.h"

#include <cstdint>
#include <limits>

namespace world::communication
{
    /** Sequential message id source (>= 1; not persisted yet). M03 Round 1:
     *  the id space never wraps - UINT64_MAX is an explicit exhaustion error
     *  so the `messageId > 0` invariant can never silently break. */
    std::uint64_t MessageIdSource::next()
    {
        if( mCurrent == std::numeric_limits<std::uint64_t>::max() )
            throw CommunicationError( "message id source exhausted (UINT64_MAX reached)" );
        return ++mCurrent;
    }

    CommunicationEnvelope makeReply( const CommunicationEnvelope &cause,
                                     CommandResultPayload result,
                                     MessageIdSource &ids )
    {
        CommunicationEnvelope reply;
        reply.messageId = ids.next();
        reply.kind = EnvelopeKind::Reply;
        // The cause's sender stays the logical owner of the request. If the
        // cause explicitly named a NON-EMPTY reply address, the reply goes
        // THERE; an empty/absent replyTo falls back to the sender so the
        // reply can never be born invalid (M02 review round 4).
        reply.sender = cause.receiver.empty() ? "world:state" : cause.receiver;
        reply.receiver = ( cause.replyTo && !cause.replyTo->empty() )
                             ? *cause.replyTo
                             : cause.sender;
        reply.context = cause.context;
        reply.action = cause.action;
        reply.target = cause.target;
        reply.payload = std::move( result );
        // The logical return address for a further answer is the cause's
        // receiver. The reply ALWAYS correlates with the concrete message it
        // answers (cause.messageId) - reusing the cause's own chain id would
        // break the reply match (round 4).
        reply.replyTo = cause.receiver.empty() ? cause.sender : cause.receiver;
        reply.correlationId = cause.messageId;
        return reply;
    }
} // namespace world::communication