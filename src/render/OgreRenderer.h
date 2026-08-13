#pragma once

#include "config/Settings.h"
#include "ui/UiConfig.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Ogre
{
    class Root;
    class SceneManager;
    class Camera;
    class Window;
    class CompositorWorkspace;
    class Light;
    class SceneNode;
    namespace v1
    {
        class OverlaySystem;
    }
}

namespace platform
{
    struct NativeWindowInfo;
}

namespace render
{
    class CrosshairOverlay;
    class DebugOverlay;

    /**
     * OgreNext renderer bound to the SDL-owned native window.
     *
     * Responsibility: create Root, load the GL3+ render system, attach an
     * Ogre window to the existing SDL window (never create its own native
     * window), render one frame per main-loop iteration, forward resizes.
     */
    class OgreRenderer
    {
    public:
        // Kept out-of-line because this class owns std::unique_ptr<DebugOverlay>
        // while DebugOverlay is intentionally only forward-declared here. An inline
        // defaulted constructor can force std::unique_ptr's cleanup path to instantiate
        // default_delete<DebugOverlay> in translation units that only see this header
        // (GCC 13 correctly rejects sizeof(incomplete type)).
        OgreRenderer();
        ~OgreRenderer();

        OgreRenderer( const OgreRenderer & ) = delete;
        OgreRenderer &operator=( const OgreRenderer & ) = delete;

        /** Set before initialize(); appearance is loaded from MODS/Default/ui.json by Application. */
        void setCrosshairConfig( const ui::CrosshairConfig &config );

        /** Set before initialize(); user/runtime renderer settings from settings.json. */
        void setOgreConfig( const config::OgreSettings &config );
        void setDebugHudConfig( const config::DebugHudSettings &config );

        /**
         * Root directory for content-relative assets (ui textures, ...).
         * Set before initialize(); may be empty when the game runs without
         * any content root (the core must still start).
         */
        void setDataDirectory( const std::filesystem::path &dataDirectory );

        bool initialize( const platform::NativeWindowInfo &nativeInfo );
        void renderFrame();

        /** Camera pose in the current local render frame (yaw/pitch in radians).
         *  Never pass absolute world coordinates here. The player flashlight
         *  follows the exact same local pose. */
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void setCameraLocalPose( float x, float y, float z, float yaw, float pitch );

        /** Player flashlight. Enabled by default; F toggles it through Application. */
        void setFlashlightEnabled( bool enabled );
        bool flashlightEnabled() const { return mFlashlightEnabled; }

        /** F5 diagnostic overlay; Application owns the values, renderer owns presentation. */
        void setDebugOverlayVisible( bool visible );
        bool debugOverlayVisible() const { return mDebugOverlayVisible; }
        void setDebugOverlayText( const std::string &text );

        /** Render statistics from Ogre's FrameStats. */
        float latestFps() const;
        float latestFrameMs() const;
        float rollingAverageFps() const;
        float rollingAverageFrameMs() const;

        /** Called from the input/system side when the window was resized. */
        void notifyWindowResized( int width, int height );

        void shutdown();

        // Exposed to scene managers (chunk world renderer).
        Ogre::Root *root() const { return mRoot; }
        Ogre::SceneManager *sceneManager() const { return mSceneManager; }
        Ogre::Camera *camera() const { return mCamera; }

    private:
        Ogre::Root *mRoot = nullptr;
        Ogre::Window *mWindow = nullptr;
        Ogre::SceneManager *mSceneManager = nullptr;
        Ogre::Camera *mCamera = nullptr;
        Ogre::CompositorWorkspace *mWorkspace = nullptr;
        Ogre::Light *mSunLight = nullptr;
        Ogre::SceneNode *mSunNode = nullptr;
        Ogre::Light *mFlashlight = nullptr;
        Ogre::SceneNode *mFlashlightNode = nullptr;
        Ogre::v1::OverlaySystem *mOverlaySystem = nullptr;
        ui::CrosshairConfig mCrosshairConfig;
        config::OgreSettings mOgreConfig;
        config::DebugHudSettings mDebugHudConfig;
        std::filesystem::path mDataDirectory;
        int mViewportHeight = 720;
        std::unique_ptr<CrosshairOverlay> mCrosshairOverlay;
        std::unique_ptr<DebugOverlay> mDebugOverlay;
        bool mFlashlightEnabled = true;
        bool mDebugOverlayVisible = false;
        bool mDebugOverlayInitAttempted = false;
    };
} // namespace render
