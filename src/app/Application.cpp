#include "app/Application.h"
#include "core/Logging.h"
#include "debug/DebugHudFormatter.h"
#include "ui/UiConfig.h"
#include "world/interaction/BlockPicker.h"
#include "world/registry/RegistryLoader.h"
#include "world/worldgen/WorldGenConfigLoader.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace app
{
    namespace
    {
        constexpr std::int64_t CAMERA_START_Y = 50;
    }

    Application::~Application() { shutdown(); }

    bool Application::initialize()
    {
        try
        {
            mSettingsPath = config::defaultSettingsPath();
            mSettings = config::loadOrCreateSettings( mSettingsPath );
            core::logInfo( "Settings: " + mSettingsPath.string() );
            mContentRoot = config::resolveContentRootFromCandidates( mSettings.mod );
            if( mContentRoot.path.empty() )
                core::logWarn( "No content root found (MODS/" + mSettings.mod +
                               " and MODS/Default missing); starting without content" );
            else
                core::logInfo( "Content root: " + mContentRoot.path.string() +
                               " (mod '" + mContentRoot.mod + "')" );
            if( !mContentRoot.path.empty() )
                mUiConfig = ui::loadUiConfig( mContentRoot.path / "ui.json" );
        }
        catch( const std::exception &error )
        {
            core::logError( std::string( "Configuration failed: " ) + error.what() );
            return false;
        }

        mPlatform = std::make_unique<platform::PlatformWindowBridge>();
        if( !mPlatform->initialize( mSettings.window.width, mSettings.window.height,
                                   mSettings.window.fullscreen, mSettings.window.resizable ) ) return false;
        mInput = std::make_unique<input::InputManager>();
        mInput->setOnShutdown( [this]() { requestShutdown(); } );
        mInput->setOnResize( [this]( int width, int height ) { if( mRenderer ) mRenderer->notifyWindowResized( width, height ); } );
        mInput->setOnMouseMotion( [this]( float dx, float dy ) {
            mCamera.rotate( static_cast<double>( dx ) * -mSettings.camera.mouseSensitivity,
                            static_cast<double>( dy ) * -mSettings.camera.mouseSensitivity );
        } );
        mInput->setOnKey( [this]( int scancode, bool pressed ) {
            if( !pressed || !mRenderer ) return;
            if( scancode == SDL_SCANCODE_F ) mRenderer->setFlashlightEnabled( !mRenderer->flashlightEnabled() );
            else if( scancode == SDL_SCANCODE_F5 ) {
                const bool show = !mRenderer->debugOverlayVisible();
                mRenderer->setDebugOverlayVisible( show );
                if( show ) updateDebugOverlay( std::chrono::steady_clock::now(), true );
            }
        } );

        mRenderer = std::make_unique<render::OgreRenderer>();
        mRenderer->setCrosshairConfig( mUiConfig.crosshair );
        mRenderer->setDataDirectory( mContentRoot.path );
        mRenderer->setOgreConfig( mSettings.ogre );
        mRenderer->setDebugHudConfig( mSettings.debugHud );
        platform::NativeWindowInfo nativeInfo;
        if( !mPlatform->getNativeWindowInfo( nativeInfo ) ) return false;
        if( !mRenderer->initialize( nativeInfo ) ) { core::logError( "Renderer initialization failed" ); return false; }
        if( !initializeWorld() ) return false;

        mWorldRenderer = std::make_unique<render::ChunkWorldRenderer>(
            mRenderer->root(), mRenderer->sceneManager(), mBlocks, mIdTable, mContentRoot.path.string() );
        if( !mWorldRenderer->initialize() ) { core::logError( "World renderer initialization failed" ); return false; }
        mSelectionRenderer = std::make_unique<render::BlockSelectionRenderer>(
            mRenderer->root(), mRenderer->sceneManager(), mUiConfig.blockSelection );
        if( !mSelectionRenderer->initialize() ) { core::logError( "Block selection renderer initialization failed" ); return false; }
        updateCameraView();
        mPlatform->setRelativeMouseMode( true );
        core::logInfo( "Mouse captured; ESC = shutdown, F = flashlight, F5 = debug HUD" );
        mLastTick = std::chrono::steady_clock::now();
        mLastDebugUpdate = mLastTick;
        mRunning = true;
        return true;
    }

    bool Application::initializeWorld()
    {
        if( !mContentRoot.path.empty() )
        {
            try { world::RegistryLoader::loadFromDirectory( mContentRoot.path, mBlocks, mBiomes, mResources ); }
            catch( const world::RegistryError &error ) { core::logError( std::string( "Registry load failed: " ) + error.what() ); return false; }
            mIdTable = world::BlockIdTable( mBlocks );
            try {
                world::RegistryLoader::loadPrototypes( mContentRoot.path, mBlocks, mPrototypes );
            } catch( const world::RegistryError &error ) { core::logError( std::string( "Prototype registry load failed: " ) + error.what() ); return false; }
            mPrototypeIds = std::make_unique<world::PrototypeIdTable>( mPrototypes );
            try {
                world::RegistryLoader::loadSidecars( mContentRoot.path, mSidecars );
            } catch( const world::RegistryError &error ) { core::logError( std::string( "Sidecar registry load failed: " ) + error.what() ); return false; }
            core::logInfo( "Registered " + std::to_string( mPrototypes.size() ) + " prototype(s) and " +
                           std::to_string( mSidecars.size() ) + " sidecar type(s)" );
            try {
                mGenConfig = worldgen::loadWorldGenConfig( mContentRoot.path / "worldgen.json" );
                mWorldGen = std::make_unique<worldgen::WorldGen>( mGenConfig, mBlocks, mIdTable, mBiomes );
            } catch( const std::exception &error ) { core::logError( std::string( "Worldgen initialization failed: " ) + error.what() ); return false; }
        }
        else
        {
            // Core must still start without any content: empty registries,
            // no worldgen, no streaming. Gameplay hooks simply do nothing.
            core::logWarn( "Skipping world initialization: no content available" );
            return true;
        }

        mStreaming = std::make_unique<world::ChunkStreamingManager>(
            *mWorldGen, mChunks, mSettings.world.chunkRenderDistance, mSettings.world.chunkCommitsPerUpdate );
        constexpr std::int64_t CAMERA_START_X = 128;
        constexpr std::int64_t CAMERA_START_Z = 128;
        const world::BlockAddress startColumn = world::fromOriginOffset( CAMERA_START_X, 0, CAMERA_START_Z );
        const std::int64_t generatedSurface = mWorldGen->surfaceHeight( startColumn );
        const std::int64_t cameraStartY = std::max<std::int64_t>( CAMERA_START_Y, generatedSurface + 24 );
        const world::BlockAddress start = world::withOriginRelativeY( startColumn, cameraStartY );
        mDynamicBridge.setAnchor( world::originBlockAddress() );
        const world::WorldPosition startWorld = world::WorldPosition::fromBlockAddress( start );
        mCamera.setPosition( mDynamicBridge.toLocal( startWorld, mDynamicSpace.edgeBlocks() ) );
        mCamera.setOrientation( 0.5, -0.35 );
        mCamera.setSpeed( mSettings.camera.moveSpeed );
        mRenderAnchor.teleportTo( startWorld );
        core::logInfo( "World initialized" );
        return true;
    }

    void Application::requestShutdown() { if( mRunning ) core::logInfo( "Shutdown requested" ); if( mPlatform ) mPlatform->setRelativeMouseMode( false ); mRunning = false; }
    std::uint32_t Application::getWindowId() const { return mPlatform ? mPlatform->getWindowId() : 0; }
    bool Application::resizeWindow( int width, int height ) { return mPlatform && mPlatform->resizeTo( width, height ); }
    world::WorldPosition Application::cameraWorldPosition() const { return mDynamicBridge.toWorld( mCamera.position() ); }
    world::BlockAddress Application::cameraBlock() const { return cameraWorldPosition().blockAddress(); }

    void Application::maybeRebaseDynamicSpace()
    {
        const spatial::dynamic::RebaseDelta delta = mDynamicSpace.rebaseDeltaFor( mCamera.position() );
        if( !delta.any() ) return;
        auto rebasedCamera = mCamera.position();
        mDynamicSpace.applyRebase( rebasedCamera, delta );
        mDynamicBridge.shiftAnchor( delta );
        mCamera.setPosition( rebasedCamera );
        core::logInfo( "DynamicSpace rebase: " + std::to_string( delta.x ) + "/" + std::to_string( delta.y ) + "/" + std::to_string( delta.z ) );
    }

    void Application::moveCamera( float dtSeconds )
    {
        const double fwd = ( mInput->isKeyDown( SDL_SCANCODE_W ) ? 1.0 : 0.0 ) - ( mInput->isKeyDown( SDL_SCANCODE_S ) ? 1.0 : 0.0 );
        const double right = ( mInput->isKeyDown( SDL_SCANCODE_D ) ? 1.0 : 0.0 ) - ( mInput->isKeyDown( SDL_SCANCODE_A ) ? 1.0 : 0.0 );
        const double up = ( mInput->isKeyDown( SDL_SCANCODE_SPACE ) ? 1.0 : 0.0 ) - ( mInput->isKeyDown( SDL_SCANCODE_LSHIFT ) ? 1.0 : 0.0 );
        mCamera.update( dtSeconds, fwd, right, up );
        maybeRebaseDynamicSpace();
    }

    void Application::updateCameraView()
    {
        const world::WorldPosition position = cameraWorldPosition();
        mRenderAnchor.update( position );
        const world::GroupAddress renderOrigin = mRenderAnchor.owner();
        if( mWorldRenderer ) mWorldRenderer->setRenderAnchor( renderOrigin );
        if( mSelectionRenderer ) mSelectionRenderer->setRenderAnchor( renderOrigin );
        const world::RelativePosition3f local = position.relativeToGroup( renderOrigin );
        mRenderer->setCameraLocalPose( local.x, local.y, local.z, static_cast<float>( mCamera.yaw() ), static_cast<float>( mCamera.pitch() ) );
        if( mStreaming ) mStreaming->update( position.blockAddress() );
    }

    void Application::updateBlockTarget()
    {
        if( !mUiConfig.blockSelection.enabled ) { mTargetBlock.reset(); if( mSelectionRenderer ) mSelectionRenderer->setSelection( std::nullopt ); return; }
        const camera::MovementBasis basis = mCamera.basis();
        mTargetBlock = world::interaction::pickBlock( mChunks, cameraWorldPosition(), basis.forward.x, basis.forward.y, basis.forward.z, mUiConfig.blockSelection.maxDistance );
        if( mSelectionRenderer ) mSelectionRenderer->setSelection( mTargetBlock ? std::optional<world::BlockAddress>( mTargetBlock->block ) : std::nullopt );
    }

    std::string Application::buildDebugOverlayText() const
    {
        if( !mRenderer || !mWorldGen ) return "Omnigrid debug: initializing...";
        const world::WorldPosition position = cameraWorldPosition();
        const world::BlockAddress &block = position.blockAddress();
        const world::ChunkAddress &chunk = block.chunk;
        const world::GroupAddress &group = chunk.group;
        const world::GroupAddress renderGroup = mRenderAnchor.owner();
        debug::DebugHudSnapshot snapshot;
        snapshot.latestFps = mRenderer->latestFps(); snapshot.averageFps = mRenderer->rollingAverageFps();
        snapshot.latestFrameMs = mRenderer->latestFrameMs(); snapshot.averageFrameMs = mRenderer->rollingAverageFrameMs();
        const world::RelativePosition3f renderLocal = position.relativeToGroup( renderGroup );
        snapshot.renderLocalX = renderLocal.x; snapshot.renderLocalY = renderLocal.y; snapshot.renderLocalZ = renderLocal.z;
        snapshot.fractionX = position.fractionX(); snapshot.fractionY = position.fractionY(); snapshot.fractionZ = position.fractionZ();
        snapshot.sectorX = group.sector.x; snapshot.sectorY = group.sector.y; snapshot.sectorZ = group.sector.z;
        snapshot.regionX = group.region.x; snapshot.regionY = group.region.y; snapshot.regionZ = group.region.z;
        snapshot.sectionX = group.section.x; snapshot.sectionY = group.section.y; snapshot.sectionZ = group.section.z;
        snapshot.groupX = group.group.x; snapshot.groupY = group.group.y; snapshot.groupZ = group.group.z;
        snapshot.chunkX = chunk.chunk.x; snapshot.chunkY = chunk.chunk.y; snapshot.chunkZ = chunk.chunk.z;
        snapshot.blockX = block.block.x; snapshot.blockY = block.block.y; snapshot.blockZ = block.block.z;
        snapshot.dynamicLocalX = mCamera.position().x; snapshot.dynamicLocalY = mCamera.position().y; snapshot.dynamicLocalZ = mCamera.position().z;
        snapshot.dynamicEdgeBlocks = mDynamicSpace.edgeBlocks();
        snapshot.renderSectorX = renderGroup.sector.x; snapshot.renderSectorY = renderGroup.sector.y; snapshot.renderSectorZ = renderGroup.sector.z;
        snapshot.renderRegionX = renderGroup.region.x; snapshot.renderRegionY = renderGroup.region.y; snapshot.renderRegionZ = renderGroup.region.z;
        snapshot.renderSectionX = renderGroup.section.x; snapshot.renderSectionY = renderGroup.section.y; snapshot.renderSectionZ = renderGroup.section.z;
        snapshot.renderGroupX = renderGroup.group.x; snapshot.renderGroupY = renderGroup.group.y; snapshot.renderGroupZ = renderGroup.group.z;
        snapshot.groupEdgeBlocks = world::BLOCKS_PER_GROUP_EDGE;
        if( const auto voxel = mChunks.tryBlockAt( block ) ) { snapshot.voxelLoaded = true; snapshot.voxelRuntimeId = *voxel; snapshot.voxelId = mIdTable.idOf( snapshot.voxelRuntimeId ); }
        if( mTargetBlock ) {
            snapshot.targetPresent = true; snapshot.targetRuntimeId = mTargetBlock->runtimeId; snapshot.targetDistance = mTargetBlock->distance; snapshot.targetId = mIdTable.idOf( mTargetBlock->runtimeId );
            const world::BlockDef &def = mBlocks.get( snapshot.targetId );
            snapshot.targetDisplayName = def.displayName; snapshot.targetTexture = def.texture; snapshot.targetTags = def.tags;
            snapshot.targetSolid = def.solid; snapshot.targetOpaque = def.opaque; snapshot.targetTransparent = def.transparent;
            const world::BlockAddress &target = mTargetBlock->block; const world::ChunkAddress &targetChunk = target.chunk; const world::GroupAddress &targetGroup = targetChunk.group;
            snapshot.targetSectorX = targetGroup.sector.x; snapshot.targetSectorY = targetGroup.sector.y; snapshot.targetSectorZ = targetGroup.sector.z;
            snapshot.targetRegionX = targetGroup.region.x; snapshot.targetRegionY = targetGroup.region.y; snapshot.targetRegionZ = targetGroup.region.z;
            snapshot.targetSectionX = targetGroup.section.x; snapshot.targetSectionY = targetGroup.section.y; snapshot.targetSectionZ = targetGroup.section.z;
            snapshot.targetGroupX = targetGroup.group.x; snapshot.targetGroupY = targetGroup.group.y; snapshot.targetGroupZ = targetGroup.group.z;
            snapshot.targetChunkX = targetChunk.chunk.x; snapshot.targetChunkY = targetChunk.chunk.y; snapshot.targetChunkZ = targetChunk.chunk.z;
            snapshot.targetBlockX = target.block.x; snapshot.targetBlockY = target.block.y; snapshot.targetBlockZ = target.block.z;
        }
        snapshot.loadedChunks = mChunks.chunkCount(); snapshot.loadedGroups = mChunks.groupCount();
        if( mStreaming ) { snapshot.streamingRadius = mStreaming->viewRadius(); snapshot.generatedChunks = mStreaming->generatedCount(); snapshot.evictedChunks = mStreaming->evictedCount(); snapshot.queuedChunks = mStreaming->queuedCount(); snapshot.readyChunks = mStreaming->readyCount(); }
        constexpr double RAD_TO_DEG = 57.2957795130823208768;
        snapshot.yawDegrees = mCamera.yaw() * RAD_TO_DEG; snapshot.pitchDegrees = mCamera.pitch() * RAD_TO_DEG;
        snapshot.flashlightEnabled = mRenderer->flashlightEnabled();
        return debug::formatDebugHud( snapshot );
    }

    void Application::updateDebugOverlay( std::chrono::steady_clock::time_point now, bool force )
    {
        if( !mRenderer || !mRenderer->debugOverlayVisible() ) return;
        constexpr auto interval = std::chrono::milliseconds( 200 );
        if( !force && now - mLastDebugUpdate < interval ) return;
        mLastDebugUpdate = now; mRenderer->setDebugOverlayText( buildDebugOverlayText() );
    }

    void Application::runFrameUpdate()
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::min( std::chrono::duration_cast<std::chrono::duration<float>>( now - mLastTick ).count(), 0.1f );
        mLastTick = now; mInput->pollEvents(); if( !mRunning ) return; moveCamera( dt ); updateCameraView();
        if( mWorldRenderer ) mWorldRenderer->sync( mChunks );
        updateBlockTarget(); updateDebugOverlay( now );
    }
    void Application::renderFrame() { if( mRenderer ) mRenderer->renderFrame(); }
    void Application::shutdown()
    {
        if( !mRunning && !mRenderer ) return;
        mRunning = false; mTargetBlock.reset(); mSelectionRenderer.reset(); mWorldRenderer.reset(); mStreaming.reset(); mWorldGen.reset();
        if( mRenderer ) { mRenderer->shutdown(); mRenderer.reset(); }
        mInput.reset(); if( mPlatform ) { mPlatform->shutdown(); mPlatform.reset(); }
    }
} // namespace app
