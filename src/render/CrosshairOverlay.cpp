#include "render/CrosshairOverlay.h"

#include "core/Logging.h"

#include <Math/Simple/OgreAabb.h>
#include <OgreBlendMode.h>
#include <OgreCamera.h>
#include <OgreColourValue.h>
#include <OgreException.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsSamplerblock.h>
#include <OgreManualObject2.h>
#include <OgreMath.h>
#include <OgreQuaternion.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreVector3.h>
#include <Hlms/Unlit/OgreHlmsUnlit.h>
#include <Hlms/Unlit/OgreHlmsUnlitDatablock.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace render
{
    namespace
    {
        constexpr const char *UI_RESOURCE_GROUP = "ClonecraftUI";
        // OgreNext default queue modes: 200-224 are FAST (v2), while 225-255 are
        // V1_FAST. ManualObject is a v2 renderable, so it must stay in a FAST queue.
        constexpr Ogre::uint8 CROSSHAIR_RENDER_QUEUE = 224u;
    }

    bool CrosshairOverlay::initialize( Ogre::Root *root, Ogre::SceneManager *sceneManager,
                                       Ogre::Camera *camera,
                                       const ui::CrosshairConfig &config,
                                       const std::filesystem::path &dataDirectory,
                                       int viewportWidth, int viewportHeight )
    {
        mRoot = root;
        mSceneManager = sceneManager;
        mCamera = camera;
        mConfig = config;
        mViewportWidth = std::max( viewportWidth, 1 );
        mViewportHeight = std::max( viewportHeight, 1 );
        if( !mConfig.enabled )
            return true;
        if( !mRoot || !mSceneManager || !mCamera )
            return false;

        try
        {
            const std::filesystem::path texturePath = dataDirectory / mConfig.texture;
            if( !std::filesystem::is_regular_file( texturePath ) )
            {
                core::logError( "Crosshair texture not found: " + texturePath.string() );
                return false;
            }

            if( !createMaterialAndTexture( texturePath ) )
                return false;

            mManual = mSceneManager->createManualObject( Ogre::SCENE_DYNAMIC );
            mManual->setCastShadows( false );
            mManual->setRenderQueueGroup( CROSSHAIR_RENDER_QUEUE );

            mNode = mSceneManager->createSceneNode( Ogre::SCENE_DYNAMIC );
            mSceneManager->getRootSceneNode()->addChild( mNode );
            mNode->attachObject( mManual );
            rebuildGeometry();

            core::logInfo( "Crosshair initialized as OgreNext HLMS quad from " +
                           texturePath.string() );
            return true;
        }
        catch( const Ogre::Exception &error )
        {
            core::logError( std::string( "Crosshair initialization failed: " ) +
                            error.getFullDescription() );
            shutdown();
            return false;
        }
        catch( const std::exception &error )
        {
            core::logError( std::string( "Crosshair initialization failed: " ) + error.what() );
            shutdown();
            return false;
        }
    }

    bool CrosshairOverlay::createMaterialAndTexture( const std::filesystem::path &texturePath )
    {
        Ogre::ResourceGroupManager &resources = Ogre::ResourceGroupManager::getSingleton();
        if( !resources.resourceGroupExists( UI_RESOURCE_GROUP ) )
            resources.createResourceGroup( UI_RESOURCE_GROUP );

        const std::filesystem::path resourceDirectory = texturePath.parent_path().lexically_normal();
        const std::string resourceDirectoryText = resourceDirectory.string();
        if( !resources.resourceLocationExists( resourceDirectoryText, UI_RESOURCE_GROUP ) )
        {
            resources.addResourceLocation( resourceDirectoryText, "FileSystem", UI_RESOURCE_GROUP,
                                           false );
        }
        if( !resources.isResourceGroupInitialised( UI_RESOURCE_GROUP ) )
            resources.initialiseResourceGroup( UI_RESOURCE_GROUP, true );

        Ogre::TextureGpuManager *textureManager =
            mRoot->getRenderSystem()->getTextureGpuManager();
        const std::string resourceName = texturePath.filename().string();
        mTexture = textureManager->createOrRetrieveTexture(
            resourceName, mTextureAlias, Ogre::GpuPageOutStrategy::Discard,
            Ogre::CommonTextureTypes::Diffuse, UI_RESOURCE_GROUP, 0u );
        if( !mTexture )
            return false;
        mTexture->scheduleTransitionTo( Ogre::GpuResidency::Resident );

        Ogre::HlmsUnlit *hlmsUnlit = static_cast<Ogre::HlmsUnlit *>(
            mRoot->getHlmsManager()->getHlms( Ogre::HLMS_UNLIT ) );
        if( !hlmsUnlit )
        {
            core::logError( "Crosshair renderer requires HLMS Unlit" );
            return false;
        }

        Ogre::HlmsMacroblock macroblock;
        macroblock.mDepthCheck = false;
        macroblock.mDepthWrite = false;

        Ogre::HlmsBlendblock blendblock;
        blendblock.setBlendType( Ogre::SBT_TRANSPARENT_ALPHA );

        mDatablock = static_cast<Ogre::HlmsUnlitDatablock *>(
            hlmsUnlit->createDatablock( Ogre::IdString( mMaterialName ), mMaterialName,
                                        macroblock, blendblock, Ogre::HlmsParamVec() ) );
        if( !mDatablock )
            return false;

        mDatablock->setUseColour( true );
        mDatablock->setColour( Ogre::ColourValue( 1.0f, 1.0f, 1.0f, mConfig.opacity ) );

        Ogre::HlmsSamplerblock samplerblock;
        samplerblock.setAddressingMode( Ogre::TAM_CLAMP );
        samplerblock.setFiltering( Ogre::TFO_NONE );
        mDatablock->setTexture( 0u, mTexture, &samplerblock );
        return true;
    }

    void CrosshairOverlay::setViewportSize( int width, int height )
    {
        mViewportWidth = std::max( width, 1 );
        mViewportHeight = std::max( height, 1 );
        rebuildGeometry();
    }

    void CrosshairOverlay::setLocalPose( float x, float y, float z, float yaw, float pitch )
    {
        if( !mNode )
            return;

        const Ogre::Quaternion heading( Ogre::Radian( yaw ), Ogre::Vector3::UNIT_Y );
        const Ogre::Quaternion tilt( Ogre::Radian( pitch ), Ogre::Vector3::UNIT_X );
        mNode->setPosition( x, y, z );
        mNode->setOrientation( heading * tilt );
    }

    void CrosshairOverlay::rebuildGeometry()
    {
        if( !mManual || !mCamera || !mConfig.enabled )
            return;

        // A camera-local quad keeps the crosshair in the exact screen center.
        // Its world-space size is derived from FOV and viewport height so the
        // configured size stays constant in physical pixels across resolutions.
        const float nearClip = std::max( static_cast<float>( mCamera->getNearClipDistance() ), 0.001f );
        const float distance = std::max( nearClip * 2.0f, 0.25f );
        const float fovY = static_cast<float>( mCamera->getFOVy().valueRadians() );
        const float viewHeight = 2.0f * distance * std::tan( fovY * 0.5f );
        const float side = viewHeight * static_cast<float>( mConfig.sizePixels ) /
                           static_cast<float>( mViewportHeight );
        const float half = side * 0.5f;
        const float z = -distance;

        mManual->clear();
        mManual->estimateVertexCount( 4u );
        mManual->estimateIndexCount( 6u );
        mManual->begin( mMaterialName, Ogre::OT_TRIANGLE_LIST );

        mManual->position( -half,  half, z );
        mManual->textureCoord( 0.0f, 0.0f );
        mManual->position(  half,  half, z );
        mManual->textureCoord( 1.0f, 0.0f );
        mManual->position(  half, -half, z );
        mManual->textureCoord( 1.0f, 1.0f );
        mManual->position( -half, -half, z );
        mManual->textureCoord( 0.0f, 1.0f );
        mManual->triangle( 0u, 2u, 1u );
        mManual->triangle( 0u, 3u, 2u );
        mManual->end();

        mManual->setLocalAabb( Ogre::Aabb( Ogre::Vector3( 0.0f, 0.0f, z ),
                                           Ogre::Vector3( half, half, 0.01f ) ) );
    }

    void CrosshairOverlay::shutdown()
    {
        if( mSceneManager )
        {
            if( mManual )
            {
                mSceneManager->destroyManualObject( mManual );
                mManual = nullptr;
            }
            if( mNode )
            {
                mSceneManager->destroySceneNode( mNode );
                mNode = nullptr;
            }
        }

        if( mRoot && mDatablock )
        {
            if( auto *hlmsUnlit = static_cast<Ogre::HlmsUnlit *>(
                    mRoot->getHlmsManager()->getHlms( Ogre::HLMS_UNLIT ) ) )
            {
                hlmsUnlit->destroyDatablock( Ogre::IdString( mMaterialName ) );
            }
            mDatablock = nullptr;
        }

        if( mRoot && mTexture )
        {
            if( Ogre::RenderSystem *renderSystem = mRoot->getRenderSystem() )
            {
                if( Ogre::TextureGpuManager *textureManager = renderSystem->getTextureGpuManager() )
                    textureManager->destroyTexture( mTexture );
            }
            mTexture = nullptr;
        }

        mCamera = nullptr;
        mSceneManager = nullptr;
        mRoot = nullptr;
    }
} // namespace render
