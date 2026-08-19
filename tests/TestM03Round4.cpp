#include "TestHarness.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/communication/SchedulerClock.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/scripting/GameplayContentRuntime.h"
#include "world/state/WorldState.h"
#include "world/worldgen/WorldGen.h"
#include "world/worldgen/WorldGenConfigLoader.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace world;
    using namespace world::communication;
    using namespace world::scripting;

    constexpr std::uint32_t RED_TINT = 0xFF4040FFu;
    constexpr std::uint32_t GREEN_TINT = 0x40FF40FFu;
    constexpr const char *VISUAL_TINT = "test:visual_tint";
    constexpr const char *CALLBACK_COUNT = "test:callback_count";
    constexpr const char *PEER_EVENT = "test:peer.change_color";
    constexpr const char *TIMER_EVENT = "test:timer.elapsed";
    const std::filesystem::path DATA_DIR = OMNIGRID_DATA_DIR;

    using Time = SchedulerClock::Time;

    class TestSchedulerClock final : public SchedulerClock
    {
    public:
        Time now() const override
        {
            std::lock_guard<std::mutex> lock( mMutex );
            return mNow;
        }

        void waitUntil( Time until ) override
        {
            std::unique_lock<std::mutex> lock( mMutex );
            if( mPendingInterrupt )
            {
                mPendingInterrupt = false;
                return;
            }
            while( mNow < until && !mPendingInterrupt )
                mCv.wait( lock );
            mPendingInterrupt = false;
        }

        void interrupt() override
        {
            {
                std::lock_guard<std::mutex> lock( mMutex );
                mPendingInterrupt = true;
            }
            mCv.notify_all();
        }

        void advance( std::chrono::steady_clock::duration delta )
        {
            {
                std::lock_guard<std::mutex> lock( mMutex );
                mNow += delta;
            }
            mCv.notify_all();
        }

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;
        Time mNow{};
        bool mPendingInterrupt = false;
    };

    bool waitFor( const std::function<bool()> &condition, int timeoutMs = 1000 )
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        while( std::chrono::steady_clock::now() < deadline )
        {
            if( condition() )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return condition();
    }

    struct Fixture
    {
        TestSchedulerClock clock;
        DelayedMessageScheduler scheduler{ clock, 64 };
        ChunkManager chunks;
        BlockRegistry blocks;
        BiomeRegistry biomes;
        ResourceRegistry resources;
        SidecarRegistry sidecars;
        PrototypeRegistry prototypes;
        BlockIdTable idTable;
        WorldState state{ chunks, idTable, sidecars, prototypes };
        CommunicationRuntime bus;
        std::shared_ptr<GameplayContentRuntime> content;
        BlockAddress aPos{};
        BlockAddress bPos{};
        std::uint16_t aId = 0;
        std::uint16_t bId = 0;

        Fixture() : bus( 256, 256 )
        {
            RegistryLoader::loadFromDirectory( DATA_DIR, blocks, biomes, resources );
            idTable = BlockIdTable( blocks );
            RegistryLoader::loadSidecars( DATA_DIR, sidecars );
            RegistryLoader::loadPrototypes( DATA_DIR, blocks, prototypes, &sidecars );
            registerBlockCommandHandlers( bus, state );
            content = GameplayContentRuntime::loadIfPresent(
                DATA_DIR, bus, scheduler, state, idTable,
                []( const BlockAddress & ) { return std::int64_t{ 60 }; } );
            CHECK( content != nullptr );
            const auto a = content->placementAddress( "proof_a" );
            const auto b = content->placementAddress( "proof_b" );
            CHECK( a.has_value() );
            CHECK( b.has_value() );
            if( a ) aPos = *a;
            if( b ) bPos = *b;
            aId = idTable.indexOf( "test:block_a" );
            bId = idTable.indexOf( "test:block_b" );
        }

        void materializeEmptyTargets()
        {
            chunks.loadChunk( aPos.chunk );
            chunks.loadChunk( bPos.chunk );
        }

        void startProof()
        {
            materializeEmptyTargets();
            CHECK_EQ( content->updateBootstraps(), std::size_t{ 1 } );
            CHECK_EQ( content->pendingBootstrapCount(), std::size_t{ 0 } );
            CHECK_EQ( state.blockAt( aPos ).value_or( 0u ), aId );
            CHECK_EQ( state.blockAt( bPos ).value_or( 0u ), bId );
            CHECK_EQ( scheduler.scheduledCount(), std::size_t{ 1 } );
        }

        void pumpAll()
        {
            while( bus.pendingInbound() > 0u )
                (void)bus.pumpOne();
        }

        std::size_t tick( std::chrono::steady_clock::duration delta )
        {
            clock.advance( delta );
            CHECK( waitFor( [&] { return scheduler.handoffCount() >= 1u; } ) );
            const std::size_t drained = scheduler.drainDueTo( bus );
            pumpAll();
            return drained;
        }

        std::optional<std::uint32_t> property( const BlockAddress &pos,
                                               const std::string &id ) const
        {
            const auto value = state.get( pos, id );
            if( !value || !std::holds_alternative<std::uint32_t>( *value ) )
                return std::nullopt;
            return std::get<std::uint32_t>( *value );
        }
    };

    void loadRegistries( BlockRegistry &blocks, BiomeRegistry &biomes,
                         ResourceRegistry &resources, SidecarRegistry &sidecars,
                         PrototypeRegistry &prototypes, BlockIdTable &idTable )
    {
        RegistryLoader::loadFromDirectory( DATA_DIR, blocks, biomes, resources );
        idTable = BlockIdTable( blocks );
        RegistryLoader::loadSidecars( DATA_DIR, sidecars );
        RegistryLoader::loadPrototypes( DATA_DIR, blocks, prototypes, &sidecars );
    }
} // namespace

// The acceptance proof consumes the shipped gameplay.json and shipped Lua
// files. There is no embedded second copy that can drift green while content
// on disk breaks.
TEST_CASE( m3r04_shipped_two_block_content_runs_exact_six_callbacks )
{
    Fixture f;
    std::vector<CommunicationEnvelope> trace;
    f.bus.setTraceSink( [&trace]( const CommunicationEnvelope &env ) {
        trace.push_back( env );
    } );
    f.startProof();

    f.clock.advance( std::chrono::milliseconds( 4999 ) );
    CHECK( waitFor( [&] { return f.scheduler.handoffCount() == 0u; } ) );
    CHECK_EQ( f.property( f.aPos, VISUAL_TINT ).value_or( 999u ), 0u );
    CHECK_EQ( f.property( f.aPos, CALLBACK_COUNT ).value_or( 999u ), 0u );

    const std::uint32_t expected[6] = {
        RED_TINT, GREEN_TINT, RED_TINT, GREEN_TINT, RED_TINT, GREEN_TINT
    };
    for( std::uint32_t callback = 1; callback <= 6; ++callback )
    {
        const auto delta = callback == 1u ? std::chrono::milliseconds( 1 )
                                          : std::chrono::milliseconds( 5000 );
        CHECK_EQ( f.tick( delta ), std::size_t{ 1 } );
        CHECK_EQ( f.property( f.aPos, VISUAL_TINT ).value_or( 0u ),
                  expected[callback - 1u] );
        CHECK_EQ( f.property( f.aPos, CALLBACK_COUNT ).value_or( 0u ), callback );
    }

    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
    CHECK_EQ( f.property( f.bPos, VISUAL_TINT ).value_or( 0u ), GREEN_TINT );

    std::size_t peerEvents = 0;
    std::size_t timerEvents = 0;
    for( const CommunicationEnvelope &env : trace )
    {
        if( env.kind != EnvelopeKind::Reply )
            CHECK( !env.replyTo.has_value() );
        if( env.action == PEER_EVENT && env.sender == "block:a" &&
            env.receiver == "block:b" && env.target && env.target->isBlock() &&
            env.target->asBlock() == f.bPos )
            ++peerEvents;
        if( env.action == TIMER_EVENT && env.sender == "block:a" &&
            env.receiver == "block:a" )
            ++timerEvents;
    }
    CHECK_EQ( peerEvents, std::size_t{ 1 } );
    CHECK_EQ( timerEvents, std::size_t{ 6 } );
}

TEST_CASE( m3r04_removed_target_before_callback_stops_chain_safely )
{
    Fixture f;
    f.startProof();
    f.tick( std::chrono::milliseconds( 5000 ) );
    CHECK_EQ( f.property( f.aPos, CALLBACK_COUNT ).value_or( 0u ), 1u );
    CHECK( f.state.setBlock( f.aPos, 0u ) );
    f.tick( std::chrono::milliseconds( 5000 ) );
    CHECK_EQ( f.scheduler.scheduledCount(), std::size_t{ 0 } );
}

TEST_CASE( m3r04_bootstrap_waits_for_real_worldgen_materialization )
{
    TestSchedulerClock clock;
    DelayedMessageScheduler scheduler( clock, 64 );
    ChunkManager chunks;
    BlockRegistry blocks;
    BiomeRegistry biomes;
    ResourceRegistry resources;
    SidecarRegistry sidecars;
    PrototypeRegistry prototypes;
    BlockIdTable idTable;
    loadRegistries( blocks, biomes, resources, sidecars, prototypes, idTable );
    WorldState state( chunks, idTable, sidecars, prototypes );
    CommunicationRuntime bus( 256, 256 );
    registerBlockCommandHandlers( bus, state );

    const worldgen::WorldGenConfig config =
        worldgen::loadWorldGenConfig( DATA_DIR / "worldgen.json" );
    worldgen::WorldGen gen( config, blocks, idTable, biomes );
    const auto content = GameplayContentRuntime::loadIfPresent(
        DATA_DIR, bus, scheduler, state, idTable,
        [&gen]( const BlockAddress &column ) { return gen.surfaceHeight( column ); } );
    CHECK( content != nullptr );
    const BlockAddress a = *content->placementAddress( "proof_a" );
    const BlockAddress b = *content->placementAddress( "proof_b" );

    CHECK_EQ( chunks.chunkCount(), std::size_t{ 0 } );
    CHECK_EQ( content->updateBootstraps(), std::size_t{ 0 } );
    CHECK_EQ( chunks.chunkCount(), std::size_t{ 0 } );

    const std::set<ChunkAddress> targets{ a.chunk, b.chunk };
    for( const ChunkAddress &chunk : targets )
    {
        std::vector<std::uint16_t> ids( Chunk::VOLUME );
        const std::uint32_t nonAir = gen.generateChunkIds( chunk, ids );
        chunks.loadChunk( chunk )->assignBlocks( ids, nonAir );
    }
    const std::size_t materializedCount = chunks.chunkCount();
    const BlockAddress terrainBelowA = offsetBlock( a, 0, -1, 0 );
    const std::optional<std::uint16_t> terrainBefore = state.blockAt( terrainBelowA );
    CHECK( terrainBefore.has_value() );
    CHECK( terrainBefore.value_or( 0u ) != 0u );

    CHECK_EQ( content->updateBootstraps(), std::size_t{ 1 } );
    CHECK_EQ( chunks.chunkCount(), materializedCount );
    CHECK( state.blockAt( terrainBelowA ) == terrainBefore );
    CHECK_EQ( state.blockAt( a ).value_or( 0u ), idTable.indexOf( "test:block_a" ) );
    CHECK_EQ( state.blockAt( b ).value_or( 0u ), idTable.indexOf( "test:block_b" ) );
}

int main() { return test::runAll(); }
