#pragma once

#include "camera/FreeCameraController.h"
#include "config/Settings.h"
#include "input/InputManager.h"
#include "platform/PlatformWindowBridge.h"
#include "render/BlockSelectionRenderer.h"
#include "render/ChunkWorldRenderer.h"
#include "render/OgreRenderer.h"
#include "ui/UiConfig.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/ChunkStreamingManager.h"
#include "world/interaction/BlockPicker.h"
#include "world/coordinates/StickyGroupAnchor.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/worldgen/WorldGen.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace app
{
    /**
     * Application is the top-level owner. It controls the lifetimes and
     * the startup/shutdown order of every subsystem:
     *
     *   SDL window (platform bridge)
     *     -> InputManager
     *     -> OgreRenderer
     *     -> world: registries -> worldgen -> chunk manager -> streaming
     *     -> render: chunk world renderer (mesh rebuild on change)
     *     -> input: free camera (WASD + mouse look)
     *
     * The main loop drives: input polling -> camera/streaming/mesh sync ->
     * render frame. ESC / window close / quit events request a clean
     * shutdown (running=false); no hard exit is ever used.
     */
    class Application
    {
    public:
        ~Application();

        bool initialize();

        /** One variable-rate application update driven once per rendered frame. */
        void runFrameUpdate();

        /** Renders one frame (runs at the render rate, not the fixed rate). */
        void renderFrame();

        void requestShutdown();
        bool isRunning() const { return mRunning; }

        /** SDL window id used by the acceptance smoke tests. */
        std::uint32_t getWindowId() const;

        /** Requests a resize through SDL; the real resize event then flows
         *  through the normal event pipeline. Used by smoke tests. */
        bool resizeWindow( int width, int height );

        /** Ordered teardown of all subsystems. */
        void shutdown();

    private:
        bool initializeWorld();
        world::BlockAddress cameraBlock() const;
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

        // World state (milestone 06 wiring).
        world::BlockRegistry mBlocks;
        world::BiomeRegistry mBiomes;
        world::ResourceRegistry mResources;
        world::BlockIdTable mIdTable;

        worldgen::WorldGenConfig mGenConfig;
        std::unique_ptr<worldgen::WorldGen> mWorldGen;
        world::ChunkManager mChunks;
        std::unique_ptr<world::ChunkStreamingManager> mStreaming;

        camera::FreeCameraController mCamera;
        world::StickyGroupAnchor mRenderAnchor;

        std::chrono::steady_clock::time_point mLastTick;
        std::chrono::steady_clock::time_point mLastDebugUpdate;

        bool mRunning = false;
    };
} // namespace app