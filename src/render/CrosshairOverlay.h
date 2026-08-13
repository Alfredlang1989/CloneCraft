#pragma once

#include "ui/UiConfig.h"

#include <filesystem>
#include <string>

namespace Ogre
{
    class Camera;
    class HlmsUnlitDatablock;
    class ManualObject;
    class Root;
    class SceneManager;
    class SceneNode;
    class TextureGpu;
}

namespace render
{
    /**
     * Camera-locked OgreNext HLMS crosshair.
     *
     * Despite the historic class name this no longer uses Ogre v1::Overlay.
     * A tiny unlit textured quad is kept in camera-local space instead. This
     * avoids the legacy fixed-function material path which OgreNext 4.x no
     * longer provides and which could crash on the first rendered frame.
     */
    class CrosshairOverlay
    {
    public:
        CrosshairOverlay() = default;
        ~CrosshairOverlay() = default;

        CrosshairOverlay( const CrosshairOverlay & ) = delete;
        CrosshairOverlay &operator=( const CrosshairOverlay & ) = delete;

        bool initialize( Ogre::Root *root, Ogre::SceneManager *sceneManager, Ogre::Camera *camera,
                         const ui::CrosshairConfig &config,
                         const std::filesystem::path &dataDirectory,
                         int viewportWidth, int viewportHeight );
        void setViewportSize( int width, int height );
        void setLocalPose( float x, float y, float z, float yaw, float pitch );
        void shutdown();

    private:
        bool createMaterialAndTexture( const std::filesystem::path &texturePath );
        void rebuildGeometry();

        Ogre::Root *mRoot = nullptr;
        Ogre::SceneManager *mSceneManager = nullptr;
        Ogre::Camera *mCamera = nullptr;
        Ogre::ManualObject *mManual = nullptr;
        Ogre::SceneNode *mNode = nullptr;
        Ogre::HlmsUnlitDatablock *mDatablock = nullptr;
        Ogre::TextureGpu *mTexture = nullptr;

        ui::CrosshairConfig mConfig;
        std::string mMaterialName = "Omnigrid/CrosshairHlms";
        std::string mTextureAlias = "Omnigrid/UI/Crosshair";
        int mViewportWidth = 1;
        int mViewportHeight = 1;
    };
} // namespace render
