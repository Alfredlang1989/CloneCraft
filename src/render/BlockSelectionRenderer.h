#pragma once

#include "ui/UiConfig.h"
#include "world/coordinates/Coords.h"

#include <optional>
#include <string>

namespace Ogre
{
    class Root;
    class SceneManager;
    class SceneNode;
    class ManualObject;
    class HlmsUnlitDatablock;
}

namespace render
{
    /** Draws the data-driven outline around the block under the crosshair. */
    class BlockSelectionRenderer
    {
    public:
        BlockSelectionRenderer( Ogre::Root *root, Ogre::SceneManager *sceneManager,
                                ui::BlockSelectionConfig config );
        ~BlockSelectionRenderer();

        BlockSelectionRenderer( const BlockSelectionRenderer & ) = delete;
        BlockSelectionRenderer &operator=( const BlockSelectionRenderer & ) = delete;

        bool initialize();
        void setRenderAnchor( const world::GroupAddress &group );
        void setSelection( const std::optional<world::BlockAddress> &block );
        void shutdown();

    private:
        bool createMaterial();
        void createGeometry();
        void updatePosition();

        Ogre::Root *mRoot = nullptr;
        Ogre::SceneManager *mSceneManager = nullptr;
        ui::BlockSelectionConfig mConfig;
        std::string mMaterialName = "Omnigrid/SelectionOutline";
        Ogre::HlmsUnlitDatablock *mDatablock = nullptr;
        Ogre::ManualObject *mManual = nullptr;
        Ogre::SceneNode *mNode = nullptr;
        world::GroupAddress mRenderAnchor{};
        std::optional<world::BlockAddress> mSelection;
    };
} // namespace render
