#include "render/OgreRenderer.h"
#include "render/CrosshairOverlay.h"
#include "render/DebugOverlay.h"

#include "core/Logging.h"
#include "platform/PlatformWindowBridge.h"

// OgreNext core
#include <OgreArchive.h>
#include <OgreArchiveManager.h>
#include <OgreAbiUtils.h>
#include <OgreCamera.h>
#include <OgreColourValue.h>
#include <OgreException.h>
#include <OgreHlmsManager.h>
#include <OgreIdString.h>
#include <OgreLight.h>
#include <OgreFrameStats.h>
#include <OgreMath.h>
#include <OgreQuaternion.h>
#include <OgreResourceGroupManager.h>
#include <OgreOverlaySystem.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneManagerEnumerator.h>
#include <OgreSceneNode.h>
#include <OgreStringConverter.h>
#include <OgreVector3.h>
#include <OgreWindow.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorShadowNode.h>
#include <Hlms/Pbs/OgreHlmsPbs.h>
#include <Hlms/Unlit/OgreHlmsUnlit.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace render
{
    namespace
    {
        constexpr const char *COMPOSITOR_DEF_NAME = "OmnigridDaylightWorkspace";
        constexpr const char *SHADOW_NODE_NAME = "OmnigridSunShadowNode";

        // Bright Minecraft-like overworld daylight sky (#78A7FF). This is the
        // compositor clear colour for now; a later atmosphere/sky system can
        // replace it without touching terrain materials. Keep these values in
        // the same form as the already-validated v7 sky path.
        constexpr float SKY_R = 120.0f / 255.0f;
        constexpr float SKY_G = 167.0f / 255.0f;
        constexpr float SKY_B = 255.0f / 255.0f;

        // Daylight fill. The first PBS pass looked far too close to a silhouette
        // renderer on the target Vega iGPU. Keep enough contrast for the sun and
        // shadows, but make non-sun-facing terrain readable.
        const Ogre::ColourValue AMBIENT_SKY( 0.62f, 0.70f, 0.86f );
        const Ogre::ColourValue AMBIENT_GROUND( 0.30f, 0.32f, 0.38f );

        // Camera flashlight defaults. Kept in renderer code for now because these
        // are player/render settings rather than per-block material properties.
        constexpr float FLASHLIGHT_RADIUS = 72.0f;
        constexpr float FLASHLIGHT_POWER = 4.0f; // multiplied by PI below
        constexpr float FLASHLIGHT_INNER_DEG = 22.0f;
        constexpr float FLASHLIGHT_OUTER_DEG = 52.0f;

        constexpr const char *DEBUG_RESOURCE_GROUP = "OmnigridDebug";

        /**
         * Register Ogre's official sample/debug resources without assuming they
         * have been flattened into the HLMS directory. OgreNext's own application
         * guide recommends Media/2.0/scripts/materials/Common together with
         * Media/packs/DebugPack.zip for text/debug rendering.
         *
         * Missing optional debug media must never prevent the 3D renderer from
         * starting. The DebugOverlay has a second-stage system-font fallback.
         */
        void configureDebugResourceGroup()
        {
            namespace fs = std::filesystem;
            Ogre::ResourceGroupManager &resources = Ogre::ResourceGroupManager::getSingleton();

            if( !resources.resourceGroupExists( DEBUG_RESOURCE_GROUP ) )
                resources.createResourceGroup( DEBUG_RESOURCE_GROUP );

            const fs::path mediaRoot( OGRE_NEXT_MEDIA_DIR );
            const fs::path commonScripts = mediaRoot / "2.0/scripts/materials/Common";
            const fs::path debugPack = mediaRoot / "packs/DebugPack.zip";

            if( fs::is_directory( commonScripts ) )
            {
                resources.addResourceLocation( commonScripts.string(), "FileSystem",
                                               DEBUG_RESOURCE_GROUP, true );
                core::logInfo( "Debug HUD resource path: " + commonScripts.string() );
            }
            else
            {
                core::logWarn( "Ogre debug material directory not found: " +
                               commonScripts.string() );
            }

            if( fs::is_regular_file( debugPack ) )
            {
                try
                {
                    resources.addResourceLocation( debugPack.string(), "Zip",
                                                   DEBUG_RESOURCE_GROUP, true );
                    core::logInfo( "Debug HUD resource pack: " + debugPack.string() );
                }
                catch( const Ogre::Exception &e )
                {
                    // Some custom Ogre builds disable Zip support. Keep going:
                    // DebugOverlay will try an already-installed system TTF.
                    core::logWarn( std::string( "Could not mount Ogre DebugPack.zip: " ) +
                                   e.getFullDescription() );
                }
            }
            else
            {
                core::logWarn( "Ogre DebugPack.zip not found: " + debugPack.string() );
            }
        }

        bool registerHlmsUnlit( Ogre::Root *root )
        {
            Ogre::HlmsManager *hlmsManager = root->getHlmsManager();
            if( hlmsManager->getHlms( Ogre::HLMS_UNLIT ) )
                return true;

            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFolderPaths;
            Ogre::HlmsUnlit::getDefaultPaths( mainFolderPath, libraryFolderPaths );

            Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
            Ogre::ArchiveVec libraryArchives;
            libraryArchives.reserve( libraryFolderPaths.size() );

            const Ogre::String mediaRoot = Ogre::String( OGRE_NEXT_HLMS_DIR ) + "/../";
            for( const Ogre::String &relativePath : libraryFolderPaths )
            {
                libraryArchives.push_back(
                    archives.load( mediaRoot + relativePath, "FileSystem", true ) );
            }

            Ogre::Archive *unlitFolder =
                archives.load( mediaRoot + mainFolderPath, "FileSystem", true );
            auto *hlmsUnlit = OGRE_NEW Ogre::HlmsUnlit( unlitFolder, &libraryArchives );
#ifndef NDEBUG
            hlmsUnlit->setDebugOutputPath( true, true );
#endif
            hlmsManager->registerHlms( hlmsUnlit );
            return true;
        }

        bool registerHlmsPbs( Ogre::Root *root )
        {
            Ogre::HlmsManager *hlmsManager = root->getHlmsManager();
            if( hlmsManager->getHlms( Ogre::HLMS_PBS ) )
                return true;

            // Do not hand-maintain the PBS library folder list. OgreNext 4.x
            // currently needs more than Common/{GLSL,Any} + Pbs/Any; notably
            // Pbs/Any/Main and Pbs/Any/Atmosphere contain shader pieces used by
            // the generated GLSL. Missing those pieces can generate syntactically
            // invalid shaders (e.g. an empty interface block ending in a stray '}').
            // Ask OgreNext itself for the exact folder set matching this build.
            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFolderPaths;
            Ogre::HlmsPbs::getDefaultPaths( mainFolderPath, libraryFolderPaths );

            Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
            Ogre::ArchiveVec libraryArchives;
            libraryArchives.reserve( libraryFolderPaths.size() );

            // getDefaultPaths() returns paths relative to Ogre's *Media* root,
            // e.g. "Hlms/Pbs/GLSL", not relative to Media/Hlms.
            // OGRE_NEXT_HLMS_DIR points at .../Media/Hlms, therefore step one
            // directory up before appending the paths returned by Ogre.
            // Using OGRE_NEXT_HLMS_DIR directly would produce the broken path
            // .../Media/Hlms/Hlms/Pbs/GLSL.
            const Ogre::String mediaRoot = Ogre::String( OGRE_NEXT_HLMS_DIR ) + "/../";
            core::logInfo( std::string( "Ogre Media root for HLMS: " ) + mediaRoot );

            for( const Ogre::String &relativePath : libraryFolderPaths )
            {
                libraryArchives.push_back(
                    archives.load( mediaRoot + relativePath, "FileSystem", true ) );
            }

            Ogre::Archive *pbsFolder =
                archives.load( mediaRoot + mainFolderPath, "FileSystem", true );
            auto *hlmsPbs = OGRE_NEW Ogre::HlmsPbs( pbsFolder, &libraryArchives );
#ifndef NDEBUG
            // Keep generated shaders available when a target machine reports a
            // driver/compiler problem. Ogre names them by shader hash/program id.
            hlmsPbs->setDebugOutputPath( true, true );
#endif
            // PCF 3x3 is a good prototype trade-off: visibly softer than a
            // single hard tap without jumping straight to expensive filters.
            hlmsPbs->setShadowSettings( Ogre::HlmsPbs::PCF_3x3 );
            hlmsManager->registerHlms( hlmsPbs );
            return true;
        }

        void createSunShadowNode( Ogre::Root *root )
        {
            Ogre::CompositorManager2 *compositor = root->getCompositorManager2();
            Ogre::RenderSystem *renderSystem = root->getRenderSystem();

            Ogre::ShadowNodeHelper::ShadowParamVec shadowParams;
            Ogre::ShadowNodeHelper::ShadowParam sun{};
            sun.technique = Ogre::SHADOWMAP_PSSM;
            sun.numPssmSplits = 3u;
            // Near split gets most of the pixels because block edges near the
            // player reveal aliasing first. All three live in one atlas.
            sun.resolution[0].x = 2048u;
            sun.resolution[0].y = 2048u;
            sun.resolution[1].x = 1024u;
            sun.resolution[1].y = 1024u;
            sun.resolution[2].x = 1024u;
            sun.resolution[2].y = 1024u;
            sun.atlasStart[0].x = 0u;
            sun.atlasStart[0].y = 0u;
            sun.atlasStart[1].x = 0u;
            sun.atlasStart[1].y = 2048u;
            sun.atlasStart[2].x = 1024u;
            sun.atlasStart[2].y = 2048u;
            sun.supportedLightTypes = 0u;
            sun.addLightType( Ogre::Light::LT_DIRECTIONAL );
            shadowParams.push_back( sun );

            Ogre::ShadowNodeHelper::createShadowNodeWithSettings(
                compositor, renderSystem->getCapabilities(), SHADOW_NODE_NAME,
                shadowParams, false );
        }

        Ogre::RenderSystem *getInstalledRenderSystem( Ogre::Root *root,
                                                       const std::string &preferredName )
        {
            if( !preferredName.empty() )
            {
                if( Ogre::RenderSystem *preferred = root->getRenderSystemByName( preferredName ) )
                    return preferred;
            }

            // Root::installPlugin may auto-select the first installed RenderSystem
            // if none was selected explicitly. Keep that as a compatibility fallback.
            if( Ogre::RenderSystem *selected = root->getRenderSystem() )
                return selected;

            const char *const candidates[] = {
                "OpenGL 3+ Rendering Subsystem",
                "OpenGL 3+ Renderdering Subsystem",
            };
            for( const char *name : candidates )
            {
                if( Ogre::RenderSystem *candidate = root->getRenderSystemByName( name ) )
                    return candidate;
            }
            return nullptr;
        }
    } // namespace

    // Both constructor and destructor live in this translation unit, after
    // DebugOverlay.h has provided the complete type. This is required for the
    // unique_ptr<DebugOverlay> member when callers include OgreRenderer.h only.
    OgreRenderer::OgreRenderer() = default;

    OgreRenderer::~OgreRenderer() { shutdown(); }

    void OgreRenderer::setCrosshairConfig( const ui::CrosshairConfig &config )
    {
        mCrosshairConfig = config;
    }

    void OgreRenderer::setOgreConfig( const config::OgreSettings &config )
    {
        mOgreConfig = config;
    }

    void OgreRenderer::setDebugHudConfig( const config::DebugHudSettings &config )
    {
        mDebugHudConfig = config;
    }

    bool OgreRenderer::initialize( const platform::NativeWindowInfo &nativeInfo )
    {
        try
        {
            // OgreNext 3.x/4.x strongly recommends the ABI cookie. It catches
            // mismatches between the headers used to build Omnigrid and the
            // installed OgreNext libraries/plugins before they turn into much
            // stranger runtime corruption.
            const Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
            mRoot = new Ogre::Root( &abiCookie,
                                    /*pluginFileName*/ "",
                                    /*configFileName*/ "",
                                    /*logFileName*/ mOgreConfig.logFile,
                                    /*appName*/ "Omnigrid" );
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "Ogre Root creation failed: " ) + e.getFullDescription() );
            return false;
        }

        try
        {
            const Ogre::String pluginPath = Ogre::String( OGRE_NEXT_PLUGIN_DIR ) + "/" +
                                            mOgreConfig.renderSystemPlugin;
            mRoot->loadPlugin( pluginPath, /*bOptional*/ false, /*options*/ nullptr );
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "Ogre plugin load failed: " ) + e.getFullDescription() );
            return false;
        }

        Ogre::RenderSystem *renderSystem = getInstalledRenderSystem( mRoot, mOgreConfig.renderSystem );
        if( !renderSystem )
        {
            core::logError( "Requested Ogre render system is not installed: " +
                            mOgreConfig.renderSystem );
            return false;
        }
        mRoot->setRenderSystem( renderSystem );
        core::logInfo( std::string( "Ogre render system: " ) + renderSystem->getName() );

        // Ogre RenderSystem options are intentionally data-driven. This includes
        // the required default sRGB Gamma Conversion setting, and allows future
        // backend options (VSync, FSAA, etc.) without adding one C++ field per option.
        for( const auto &[name, value] : mOgreConfig.configOptions )
        {
            try
            {
                renderSystem->setConfigOption( name, value );
                std::string message = "Ogre option: ";
                message += name;
                message += " = ";
                message += value;
                core::logInfo( message );
            }
            catch( const Ogre::Exception &e )
            {
                // Renderer/plugin builds expose different option sets. Unknown or
                // unsupported user options must not make the whole game unbootable.
                std::string message = "Ignoring unsupported Ogre option '";
                message += name;
                message += "' = '";
                message += value;
                message += "': ";
                message += e.getFullDescription();
                core::logWarn( message );
            }
        }

        // Attach Ogre to the SDL-owned native X11 window: Ogre creates
        // its own GLX child window (with a proper GL visual) inside the
        // SDL window via "parentWindowHandle".
        Ogre::NameValuePairList miscParams;
        // GLX parentWindowHandle is display*:screen:windowHandle. Passing only
        // the XID happens to work with some Ogre/GLX combinations but is not
        // the documented OgreNext GLX form and can select the wrong display
        // or screen. This matches the original SDL3/Ogre prototype that is
        // known to work on the target X11 machine.
        miscParams["parentWindowHandle"] =
            Ogre::StringConverter::toString(
                reinterpret_cast<std::uintptr_t>( nativeInfo.x11Display ) ) + ":" +
            Ogre::StringConverter::toString( nativeInfo.x11Screen ) + ":" +
            Ogre::StringConverter::toString( nativeInfo.x11Window );

        try
        {
            mRoot->initialise( /*autoCreateWindow*/ false );

            mWindow = mRoot->createRenderWindow( "OmnigridMainWindow",
                                                  nativeInfo.widthPx,
                                                  nativeInfo.heightPx,
                                                  /*fullScreen*/ false,
                                                  &miscParams );
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "Ogre window creation failed: " ) + e.getFullDescription() );
            return false;
        }

        mViewportHeight = std::max( nativeInfo.heightPx, 1 );

        mSceneManager = mRoot->createSceneManager( "DefaultSceneManager", /*numWorkerThreads*/ 0 );

        // Ogre Overlay is deliberately a renderer-side presentation service.
        // Its constructor registers OverlayManager + FontManager. The latter must
        // exist before General is initialized so .fontdef scripts (DebugFont) can
        // be discovered from Ogre's Media tree.
        try
        {
            mOverlaySystem = OGRE_NEW Ogre::v1::OverlaySystem();
            mSceneManager->addRenderQueueListener( mOverlaySystem );
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "Ogre OverlaySystem initialization failed: " ) +
                            e.getFullDescription() );
            return false;
        }

        // Register the actual Ogre sample/debug media instead of recursively
        // searching the HLMS tree for a random TTF. FontManager is created by
        // OverlaySystem above; HLMS must be registered before resource scripts
        // are parsed (OgreNext initialization requirement).
        configureDebugResourceGroup();

        try
        {
            if( !registerHlmsUnlit( mRoot ) )
            {
                core::logError( "HLMS Unlit registration failed" );
                return false;
            }
            if( !registerHlmsPbs( mRoot ) )
            {
                core::logError( "HLMS PBS registration failed" );
                return false;
            }
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "HLMS initialization failed: " ) +
                            e.getFullDescription() );
            return false;
        }
        catch( const std::exception &e )
        {
            core::logError( std::string( "HLMS initialization failed: " ) + e.what() );
            return false;
        }
        core::logInfo( "HLMS Unlit + PBS registered" );

        try
        {
            Ogre::ResourceGroupManager &resources = Ogre::ResourceGroupManager::getSingleton();
            if( !resources.isResourceGroupInitialised( DEBUG_RESOURCE_GROUP ) )
                resources.initialiseResourceGroup( DEBUG_RESOURCE_GROUP, true );
            core::logInfo( "Ogre debug resource group initialized" );
        }
        catch( const Ogre::Exception &e )
        {
            // HUD is optional. Do not sacrifice the world renderer for missing
            // sample media or a custom Ogre build without Zip/FreeType support.
            core::logWarn( std::string( "Ogre debug resource group initialization failed: " ) +
                           e.getFullDescription() );
        }

        // Do not instantiate the HUD yet. Overlay/font resources are ready, but
        // delaying actual font texture + Unlit shader creation until F5 (or an
        // explicit startup request) ensures the main compositor/window path has
        // completed initialization first. This also keeps HUD failure entirely
        // optional for normal gameplay.

        // Outdoor daylight. PBS expects physically-oriented light powers; PI
        // gives a neutral white Lambertian surface approximately unit response.
        // A slightly warm sun plus cool hemispherical sky fill keeps voxel faces
        // readable without returning to flat/unlit rendering.
        mSceneManager->setAmbientLight( AMBIENT_SKY, AMBIENT_GROUND,
                                        Ogre::Vector3::UNIT_Y, 1.0f );
        mSceneManager->setShadowFarDistance( mOgreConfig.shadowFarDistance );

        // Non-directional lights (the flashlight now, torches later) need a
        // Forward+ light list for PBS. OgreNext's own Forward3D sample uses this
        // path for spotlights. This modest preset is plenty for one player light
        // and leaves headroom for early gameplay lights without enabling a huge
        // per-cell budget.
        mSceneManager->setForward3D( mOgreConfig.forward3d.enabled,
                                     mOgreConfig.forward3d.width,
                                     mOgreConfig.forward3d.height,
                                     mOgreConfig.forward3d.depth,
                                     mOgreConfig.forward3d.lightsPerCell,
                                     mOgreConfig.forward3d.minDistance,
                                     mOgreConfig.forward3d.maxDistance );

        mSunLight = mSceneManager->createLight();
        mSunNode = mSceneManager->createSceneNode( Ogre::SCENE_DYNAMIC );
        mSceneManager->getRootSceneNode()->addChild( mSunNode );
        mSunNode->attachObject( mSunLight );
        mSunLight->setType( Ogre::Light::LT_DIRECTIONAL );
        mSunLight->setDiffuseColour( Ogre::ColourValue( 1.0f, 0.965f, 0.86f ) );
        mSunLight->setSpecularColour( Ogre::ColourValue( 1.0f, 0.985f, 0.94f ) );
        mSunLight->setPowerScale( Ogre::Math::PI );
        mSunLight->setCastShadows( true );
        // Direction points from the light towards the scene. Angled on all
        // axes so voxel geometry gets immediately readable face shading.
        mSunLight->setDirection( Ogre::Vector3( -0.48f, -0.78f, -0.39f ).normalisedCopy() );

        // Camera-mounted flashlight. It intentionally does not cast a shadow map
        // yet: the current shadow node is dedicated to the sun, and adding a
        // shadowed player spot would cost another dynamic shadow map. The light
        // still participates in PBS diffuse/specular shading through Forward3D.
        mFlashlight = mSceneManager->createLight();
        mFlashlightNode = mSceneManager->createSceneNode( Ogre::SCENE_DYNAMIC );
        mSceneManager->getRootSceneNode()->addChild( mFlashlightNode );
        mFlashlightNode->attachObject( mFlashlight );
        mFlashlight->setType( Ogre::Light::LT_SPOTLIGHT );
        mFlashlight->setDiffuseColour( Ogre::ColourValue( 1.0f, 0.965f, 0.90f ) );
        mFlashlight->setSpecularColour( Ogre::ColourValue( 1.0f, 0.985f, 0.95f ) );
        mFlashlight->setPowerScale( Ogre::Math::PI * FLASHLIGHT_POWER );
        mFlashlight->setSpotlightRange( Ogre::Degree( FLASHLIGHT_INNER_DEG ),
                                        Ogre::Degree( FLASHLIGHT_OUTER_DEG ), 1.0f );
        mFlashlight->setAttenuationBasedOnRadius( FLASHLIGHT_RADIUS, 0.01f );
        mFlashlight->setCastShadows( false );
        mFlashlight->setVisible( mFlashlightEnabled );

        mCamera = mSceneManager->createCamera( "MainCamera" );
        mCamera->setAutoAspectRatio( true );
        mCamera->setNearClipDistance( mOgreConfig.cameraNearClip );
        mCamera->setFarClipDistance( mOgreConfig.cameraFarClip );

        Ogre::CompositorManager2 *compositor = mRoot->getCompositorManager2();
        createSunShadowNode( mRoot );
        compositor->createBasicWorkspaceDef(
            COMPOSITOR_DEF_NAME, Ogre::ColourValue( SKY_R, SKY_G, SKY_B ),
            Ogre::IdString( SHADOW_NODE_NAME ) );
        mWorkspace = compositor->addWorkspace( mSceneManager, mWindow->getTexture(), mCamera,
                                               Ogre::IdString( COMPOSITOR_DEF_NAME ), true );

        mCrosshairOverlay = std::make_unique<CrosshairOverlay>();
        if( !mCrosshairOverlay->initialize( mRoot, mSceneManager, mCamera, mCrosshairConfig, "data",
                                           nativeInfo.widthPx, nativeInfo.heightPx ) )
        {
            core::logError( "Crosshair overlay initialization failed" );
            return false;
        }

        core::logInfo( "OgreNext initialized and attached to the SDL window" );
        return true;
    }

    void OgreRenderer::renderFrame() { mRoot->renderOneFrame(); }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void OgreRenderer::setCameraLocalPose( float x, float y, float z, float yaw, float pitch )
    {
        if( !mCamera )
            return;

        constexpr float kMaxLocalCameraDistance = 16'384.0f;
        if( !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( z ) ||
            std::fabs( x ) > kMaxLocalCameraDistance ||
            std::fabs( y ) > kMaxLocalCameraDistance ||
            std::fabs( z ) > kMaxLocalCameraDistance )
        {
            throw std::runtime_error(
                "OgreRenderer rejected non-local camera coordinates; render-anchor invariant broken" );
        }

        mCamera->setPosition( x, y, z );
        // Yaw around +Y, then pitch about the resulting local +X.
        // At yaw=0, pitch=0 the camera looks along -Z.
        Ogre::Quaternion heading( Ogre::Radian( yaw ), Ogre::Vector3::UNIT_Y );
        Ogre::Quaternion tilt( Ogre::Radian( pitch ), Ogre::Vector3::UNIT_X );
        const Ogre::Quaternion orientation = heading * tilt;
        mCamera->setOrientation( orientation );
        if( mCrosshairOverlay )
            mCrosshairOverlay->setLocalPose( x, y, z, yaw, pitch );

        if( mFlashlightNode )
        {
            // A tiny downward offset keeps the light origin out of the camera
            // plane while preserving the camera's exact look direction. Ogre
            // spotlights face local -Z, matching Ogre Camera's forward axis.
            mFlashlightNode->setPosition( x, y - 0.10f, z );
            mFlashlightNode->setOrientation( orientation );
        }
    }

    void OgreRenderer::setFlashlightEnabled( bool enabled )
    {
        mFlashlightEnabled = enabled;
        if( mFlashlight )
            mFlashlight->setVisible( enabled );
        core::logInfo( std::string( "Flashlight " ) + ( enabled ? "ON" : "OFF" ) );
    }

    void OgreRenderer::setDebugOverlayVisible( bool visible )
    {
        if( visible && !mDebugOverlay && !mDebugOverlayInitAttempted )
        {
            // Lazy construction deliberately happens after Renderer::initialize
            // returned and the compositor workspace exists. If this fails, the
            // detailed resource/font stage has already been logged once.
            mDebugOverlayInitAttempted = true;
            auto overlay = std::make_unique<DebugOverlay>();
            if( overlay->initialize( mDebugHudConfig, mViewportHeight ) )
                mDebugOverlay = std::move( overlay );
        }

        if( !mDebugOverlay )
        {
            mDebugOverlayVisible = false;
            if( visible && mDebugOverlayInitAttempted )
                core::logWarn( "Debug overlay unavailable; see the preceding Debug HUD init error" );
            return;
        }

        mDebugOverlayVisible = visible;
        mDebugOverlay->setVisible( visible );
        core::logInfo( std::string( "Debug overlay " ) + ( visible ? "ON" : "OFF" ) );
    }

    void OgreRenderer::setDebugOverlayText( const std::string &text )
    {
        if( mDebugOverlay )
            mDebugOverlay->setText( text );
    }

    float OgreRenderer::latestFps() const
    {
        if( !mRoot || !mRoot->getFrameStats() )
            return 0.0f;
        return static_cast<float>( mRoot->getFrameStats()->getLatestTimeSinceLastFps() );
    }

    float OgreRenderer::latestFrameMs() const
    {
        if( !mRoot || !mRoot->getFrameStats() )
            return 0.0f;
        return static_cast<float>( mRoot->getFrameStats()->getLatestTimeSinceLast() * 1000.0 );
    }

    float OgreRenderer::rollingAverageFps() const
    {
        if( !mRoot || !mRoot->getFrameStats() )
            return 0.0f;
        return static_cast<float>( mRoot->getFrameStats()->getRollingAverageFps() );
    }

    float OgreRenderer::rollingAverageFrameMs() const
    {
        if( !mRoot || !mRoot->getFrameStats() )
            return 0.0f;
        return static_cast<float>( mRoot->getFrameStats()->getRollingAverage() * 1000.0 );
    }

    void OgreRenderer::notifyWindowResized( int width, int height )
    {
        if( !mWindow )
            return;
        // SDL owns the parent window. Keep Ogre's render target size in sync
        // and let the backend refresh any window-dependent bookkeeping.
        mWindow->requestResolution( static_cast<std::uint32_t>( width ),
                                    static_cast<std::uint32_t>( height ) );
        mWindow->windowMovedOrResized();
        if( mCamera )
            mCamera->setAutoAspectRatio( true );
        mViewportHeight = std::max( height, 1 );
        if( mCrosshairOverlay )
            mCrosshairOverlay->setViewportSize( width, height );
        if( mDebugOverlay )
            mDebugOverlay->setViewportHeight( mViewportHeight );
        core::logInfo( "Ogre window resized to " + std::to_string( width ) + "x" +
                       std::to_string( height ) );
    }

    void OgreRenderer::shutdown()
    {
        if( !mRoot )
            return;

        if( mWorkspace )
        {
            mRoot->getCompositorManager2()->removeWorkspace( mWorkspace );
            mWorkspace = nullptr;
        }

        // Overlay elements are owned by OverlayManager and disappear with
        // OverlaySystem. Drop our facades first, then remove the scene listener
        // before destroying the manager it references.
        if( mCrosshairOverlay )
            mCrosshairOverlay->shutdown();
        mCrosshairOverlay.reset();
        mDebugOverlay.reset();
        mDebugOverlayVisible = false;
        mDebugOverlayInitAttempted = false;
        if( mSceneManager && mOverlaySystem )
            mSceneManager->removeRenderQueueListener( mOverlaySystem );
        if( mOverlaySystem )
        {
            OGRE_DELETE mOverlaySystem;
            mOverlaySystem = nullptr;
        }

        if( mSceneManager )
        {
            mRoot->destroySceneManager( mSceneManager );
            mSceneManager = nullptr;
        }
        mCamera = nullptr;
        mFlashlight = nullptr;
        mFlashlightNode = nullptr;
        mSunLight = nullptr;
        mSunNode = nullptr;

        mRoot->shutdown();
        delete mRoot;
        mRoot = nullptr;
        mWindow = nullptr;

        core::logInfo( "OgreNext shut down" );
    }
} // namespace render
