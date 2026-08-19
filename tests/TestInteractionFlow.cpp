#include "TestHarness.h"
#include "world/chunk/ChunkManager.h"
#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRouter.h"
#include "world/interaction/PlayerInteractionController.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace world;
    using namespace world::communication;

    /**
     * M02-D end-to-end flow (issue #18) without the SDL window layer:
     *
     *   player raycast (BlockPicker)
     *     -> CommunicationEnvelope(kind=Command)   [place/remove intent]
     *     -> CommunicationRouter                    [handler lookup]
     *     -> WorldState                             [authoritative mutation]
     *     -> dirty/invalidation hook                [observer]
     *     -> visible change (chunks/state read-back)
     *     -> correlated Reply
     *
     * The real SDL input binding lives in the Ogre/App target; this chain is
     * the exact same contract, proven headlessly.
     */
    struct Fixture
    {
        ChunkManager chunks;
        BlockRegistry blocks;
        SidecarRegistry sidecars;
        PrototypeRegistry prototypes;
        BlockIdTable idTable;
        MemoryPersistenceSink sink;
        WorldState state;
        MessageIdSource ids;
        CommunicationRouter router;

        std::uint16_t stoneId = 0;
        int changeCallbacks = 0;

        Fixture() :
            state( chunks, idTable, sidecars, prototypes )
        {
            BlockDef air;
            air.id = "core:air";
            air.displayName = "Air";
            blocks.insert( air );
            BlockDef stone;
            stone.id = "core:stone";
            stone.displayName = "Stone";
            blocks.insert( stone );
            idTable = BlockIdTable( blocks );
            stoneId = idTable.indexOf( "core:stone" );
            state.setPersistenceSink( &sink );
            state.setOnChange( [this]( const BlockAddress &, const std::string &what ) {
                if( what == "block" ) ++changeCallbacks;
            } );
            registerBlockCommandHandlers( router, state, ids );
        }
    };

    CommunicationEnvelope commandEnvelope( MessageIdSource &ids, const std::string &action,
                                           const BlockAddress &target, Payload payload )
    {
        CommunicationEnvelope env;
        env.messageId = ids.next();
        env.kind = EnvelopeKind::Command;
        env.sender = "player:1";
        env.receiver = "world:state";
        env.context = "core:world";
        env.action = action;
        env.target = WorldStateTarget( target );
        env.payload = std::move( payload );
        return env;
    }
} // namespace

TEST_CASE( m02_end_to_end_face_placement )
{
    Fixture f;
    // Terrain ahead of the camera (the targeted block).
    const BlockAddress wall = fromOriginOffset( 4, 4, -4 );
    f.chunks.setBlock( wall, f.stoneId );

    // 1. input raycast: the camera looks along +Z at the wall.
    const WorldPosition camera =
        WorldPosition::fromBlockAddress( fromOriginOffset( 4, 4, -8 ), 0.5f, 0.5f, 0.5f );
    const auto pick = interaction::pickBlock( f.chunks, camera, 0.0, 0.0, 1.0, 16.0 );
    CHECK( pick.has_value() );
    if( !pick )
        return;
    CHECK( pick->face == interaction::BlockFace::NegativeZ );
    CHECK( pick->adjacent.has_value() );
    if( !pick->adjacent )
        return;

    // 2. the player's intent becomes a Command envelope targeted at the
    //    canonical position (M01-B WorldStateTarget).
    // target = the adjacent cell (authoritative placement address).
    const auto env = commandEnvelope( f.ids, ACTION_BLOCK_PLACE, *pick->adjacent,
                                      BlockPlacePayload{ f.stoneId } );

    // 3+4. router -> authoritative WorldState mutation.
    const auto result = f.router.dispatch( env );
    CHECK( result.handled );
    CHECK_EQ( result.replies.size(), std::size_t{ 1 } );

    // 5. dirty/invalidation hook fired for the real change.
    CHECK_EQ( f.changeCallbacks, 1 );
    CHECK( f.sink.isDirty( pick->adjacent->chunk ) );
    CHECK_EQ( f.sink.blockDeltas().at( *pick->adjacent ).newRuntimeId, f.stoneId );

    // 6. visible change: the placed block is present at the adjacent cell.
    const auto placed = f.state.blockAt( *pick->adjacent );
    CHECK( placed.has_value() );
    if( placed )
        CHECK_EQ( *placed, f.stoneId );

    // 7. correlated accepted reply.
    CHECK( result.replies[0].kind == EnvelopeKind::Reply );
    CHECK( result.replies[0].replyTo == env.receiver );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( reply->ok );
}

TEST_CASE( m02_end_to_end_block_removal )
{
    Fixture f;
    const BlockAddress target = fromOriginOffset( 1, 1, -3 );
    f.chunks.setBlock( target, f.stoneId );

    const auto camera =
        WorldPosition::fromBlockAddress( fromOriginOffset( 1, 1, -6 ), 0.5f, 0.5f, 0.5f );
    const auto pick = interaction::pickBlock( f.chunks, camera, 0.0, 0.0, 1.0, 12.0 );
    CHECK( pick.has_value() );
    if( !pick )
        return;

    const auto env = commandEnvelope( f.ids, ACTION_BLOCK_REMOVE, pick->block,
                                      std::monostate{} );
    const auto result = f.router.dispatch( env );
    CHECK( result.handled );
    const auto *reply = std::get_if<CommandResultPayload>( &result.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( reply->ok );

    const auto after = f.state.blockAt( target );
    CHECK( after.has_value() );
    if( after )
        CHECK_EQ( *after, 0u );               // back to AIR
    CHECK( f.sink.isDirty( target.chunk ) );  // persistence dirty state
    CHECK_EQ( f.changeCallbacks, 1 );         // one real invalidation
}

TEST_CASE( m02_controller_input_drives_the_same_funnel )
{
    // The controller is the exact entry the SDL host uses for mouse clicks:
    // left = place on face, right = remove. Input never reaches ChunkManager.
    // (Handlers are already bound by the Fixture.)
    Fixture f;
    interaction::PlayerInteractionController input( f.chunks, f.ids, f.router );
    input.setSelectedRuntimeId( f.stoneId );

    const BlockAddress wall = fromOriginOffset( 4, 4, -4 );
    f.chunks.setBlock( wall, f.stoneId );
    const WorldPosition camera =
        WorldPosition::fromBlockAddress( fromOriginOffset( 4, 4, -8 ), 0.5f, 0.5f, 0.5f );

    // primary click: place on the hit face (camera side cell).
    const auto direct = interaction::pickBlock( f.chunks, camera, 0.0, 0.0, 1.0, 16.0 );
    CHECK( direct.has_value() );
    const auto placed = input.pressPrimary( camera, 0.0, 0.0, 1.0, 16.0 );
    CHECK( placed.picked );
    CHECK( placed.dispatched );
    CHECK_EQ( placed.replies.size(), std::size_t{ 1 } );
    if( placed.hit && placed.hit->adjacent )
    {
        const auto cell = f.state.blockAt( *placed.hit->adjacent );
        CHECK( cell.has_value() );
        if( cell )
            CHECK_EQ( *cell, f.stoneId );
    }
    const auto *reply = std::get_if<CommandResultPayload>( &placed.replies[0].payload );
    CHECK( reply != nullptr );
    if( reply )
        CHECK( reply->ok );

    // secondary click: removes the targeted block again.
    const auto removed = input.pressSecondary( camera, 0.0, 0.0, 1.0, 16.0 );
    CHECK( removed.picked );
    // The viewer now faces the freshly placed block: the second click
    // removes exactly that visible block again.
    if( removed.hit )
    {
        const auto cell = f.state.blockAt( removed.hit->block );
        CHECK( cell.has_value() );
        if( cell )
            CHECK_EQ( *cell, 0u );
    }
    const auto *removedReply =
        std::get_if<CommandResultPayload>( &removed.replies[0].payload );
    CHECK( removedReply != nullptr );
    if( removedReply )
        CHECK( removedReply->ok );
}

int main() { return test::runAll(); }