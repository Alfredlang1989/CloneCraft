#pragma once

#include "camera/FreeCameraController.h"
#include "config/ContentRoot.h"
#include "config/Settings.h"
#include "input/InputManager.h"
#include "platform/PlatformWindowBridge.h"
#include "render/BlockSelectionRenderer.h"
#include "render/ChunkWorldRenderer.h"
#include "render/OgreRenderer.h"
#include "spatial/bridge/WorldDynamicBridge.h"
#include "spatial/dynamic/DynamicSpace.h"
#include "ui/UiConfig.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/ChunkStreamingManager.h"
#include "world/interaction/BlockPicker.h"
#include "world/interaction/PlayerInteractionController.h"
#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRouter.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/communication/SchedulerClock.h"
#include "world/coordinates/StickyGroupAnchor.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/PrototypeIdTable.h"
#include "world/registry/Registry.h"
#include "world/scripting/GameplayContentRuntime.h"
#include "world/scripting/GameplayLuaRuntime.h"
#include "world/state/MemoryPersistenceSink.h"
#include "world/state/WorldState.h"
#include "world/worldgen/WorldGen.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace app
{
    class Application
    {
    public:
        ~Application();
        bool initialize();
        void runFrameUpdate();
        void renderFrame();
        void requestShutdown();
        bool isRunning() const { return mRunning; }
        std::uint32_t getWindowId() const;
        bool resizeWindow( int width, int height );
        void shutdown();

    private:
        bool initializeWorld();
        world::WorldPosition cameraWorldPosition() const;
        world::BlockAddress cameraBlock() const;
        void maybeRebaseDynamicSpace();
        void moveCamera( float dtSeconds );
        void updateCameraView();
        void updateBlockTarget();
        void updateDebugOverlay( std::chrono::steady_clock::time_point now, bool force = false );
        std::string buildDebugOverlayText() const;
        /** M02-D: real player input -> envelope -> router -> world state.
         *  Called from the SDL event path for mouse buttons (primary =
         *  place selected block, secondary = remove). Never touches
         *  ChunkManager directly. */
        void handleBlockInteraction( bool primary );
        void logInteractionReply( const std::string &action,
                                  const world::communication::CommunicationEnvelope &reply ) const;
        /** Generic render projection for a block's data-declared packed tint
         *  property. Content owns the property id and values; this method
         *  reacts only to WorldState's normal change hook. */
        void updateBlockVisualTint( const world::BlockAddress &address,
                                    const std::string &what );

        config::Settings mSettings;
        std::filesystem::path mSettingsPath;
        std::unique_ptr<platform::PlatformWindowBridge> mPlatform;
        std::unique_ptr<input::InputManager> mInput;
        std::unique_ptr<render::OgreRenderer> mRenderer;
        std::unique_ptr<render::ChunkWorldRenderer> mWorldRenderer;
        std::unique_ptr<render::BlockSelectionRenderer> mSelectionRenderer;
        ui::UiConfig mUiConfig;
        std::optional<world::interaction::BlockPickResult> mTargetBlock;
        // M03 Round 1: one production communication bus (single message-id
        // source + bounded A/B queues + signal/slot/action registries). The
        // former separate MessageIdSource/Router members are replaced by it.
        // Gameplay input uses the bus's SYNCHRONOUS dispatch() convenience
        // (validation + router execution, outputs in the DispatchResult);
        // the A/B queues serve the async producer path (Round 2 timer
        // worker via submit()/pump*()).
        world::communication::CommunicationRuntime mCommunicationBus{ 256, 256 };
        std::unique_ptr<world::interaction::PlayerInteractionController> mPlayerInteraction;
        // M03 Round 2/4: the delayed-message scheduler (Time Worker) and the
        // gameplay Lua runtime. The scheduler stores only transportable
        // CommunicationEnvelopes; the owner/game thread drains due envelopes
        // into the bus each frame. The Lua runtime is owner-thread only and
        // MUST be destroyed before the bus/scheduler/world it references.
        world::communication::SteadySchedulerClock mSchedulerClock;
        world::communication::DelayedMessageScheduler mScheduler{ mSchedulerClock, 256 };
        std::uint16_t mSelectedRuntimeId = 0;
        world::BlockRegistry mBlocks;
        world::BiomeRegistry mBiomes;
        world::ResourceRegistry mResources;
        world::PrototypeRegistry mPrototypes;
        world::SidecarRegistry mSidecars;
        world::BlockIdTable mIdTable;
        std::unique_ptr<world::PrototypeIdTable> mPrototypeIds;
        std::unique_ptr<world::WorldState> mWorldState;
        // M03 content runtime. Declared AFTER WorldState so it and its Lua VM
        // are destroyed first (reverse declaration order). All concrete
        // scripts/actions/placements live in the selected content root's
        // gameplay.json, never in Application C++.
        std::shared_ptr<world::scripting::GameplayContentRuntime> mGameplayContent;
        bool mGameplayBootstrapFailed = false;
        world::MemoryPersistenceSink mPersistenceSink;
        config::ContentRoot mContentRoot;
        worldgen::WorldGenConfig mGenConfig;
        std::unique_ptr<worldgen::WorldGen> mWorldGen;
        world::ChunkManager mChunks;
        std::unique_ptr<world::ChunkStreamingManager> mStreaming;
        spatial::dynamic::DynamicSpace mDynamicSpace;
        spatial::bridge::WorldDynamicBridge mDynamicBridge;
        camera::FreeCameraController mCamera;
        world::StickyGroupAnchor mRenderAnchor;
        std::chrono::steady_clock::time_point mLastTick;
        std::chrono::steady_clock::time_point mLastDebugUpdate;
        bool mRunning = false;
    };
} // namespace app
