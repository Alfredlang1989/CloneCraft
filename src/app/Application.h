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
#include "world/coordinates/StickyGroupAnchor.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/PrototypeIdTable.h"
#include "world/registry/Registry.h"
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

        config::Settings mSettings;
        std::filesystem::path mSettingsPath;
        std::unique_ptr<platform::PlatformWindowBridge> mPlatform;
        std::unique_ptr<input::InputManager> mInput;
        std::unique_ptr<render::OgreRenderer> mRenderer;
        std::unique_ptr<render::ChunkWorldRenderer> mWorldRenderer;
        std::unique_ptr<render::BlockSelectionRenderer> mSelectionRenderer;
        ui::UiConfig mUiConfig;
        std::optional<world::interaction::BlockPickResult> mTargetBlock;
        world::BlockRegistry mBlocks;
        world::BiomeRegistry mBiomes;
        world::ResourceRegistry mResources;
        world::PrototypeRegistry mPrototypes;
        world::SidecarRegistry mSidecars;
        world::BlockIdTable mIdTable;
        std::unique_ptr<world::PrototypeIdTable> mPrototypeIds;
        std::unique_ptr<world::WorldState> mWorldState;
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
