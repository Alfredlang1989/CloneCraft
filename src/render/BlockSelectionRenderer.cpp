#include "render/BlockSelectionRenderer.h"

#include "core/Logging.h"

#include <Math/Simple/OgreAabb.h>
#include <OgreBlendMode.h>
#include <OgreColourValue.h>
#include <OgreException.h>
#include <OgreHlmsManager.h>
#include <OgreManualObject2.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreVector3.h>
#include <Hlms/Unlit/OgreHlmsUnlit.h>
#include <Hlms/Unlit/OgreHlmsUnlitDatablock.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace render
{
    namespace
    {
        float srgbToLinear( float value )
        {
            value = std::clamp( value, 0.0f, 1.0f );
            if( value <= 0.04045f )
                return value / 12.92f;
            return std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
        }

        void addBox( Ogre::ManualObject *manual,
                     float minX, float minY, float minZ,
                     float maxX, float maxY, float maxZ,
                     Ogre::uint32 &base )
        {
            const float p[8][3] = {
                { minX, minY, minZ }, { maxX, minY, minZ },
                { maxX, maxY, minZ }, { minX, maxY, minZ },
                { minX, minY, maxZ }, { maxX, minY, maxZ },
                { maxX, maxY, maxZ }, { minX, maxY, maxZ },
            };
            for( const auto &v : p )
                manual->position( v[0], v[1], v[2] );

            static constexpr Ogre::uint32 triangles[][3] = {
                {0,2,1}, {0,3,2}, // -Z
                {4,5,6}, {4,6,7}, // +Z
                {0,1,5}, {0,5,4}, // -Y
                {3,7,6}, {3,6,2}, // +Y
                {0,4,7}, {0,7,3}, // -X
                {1,2,6}, {1,6,5}, // +X
            };
            for( const auto &tri : triangles )
                manual->triangle( base + tri[0], base + tri[1], base + tri[2] );
            base += 8u;
        }
    } // namespace

    BlockSelectionRenderer::BlockSelectionRenderer( Ogre::Root *root,
                                                      Ogre::SceneManager *sceneManager,
                                                      ui::BlockSelectionConfig config ) :
        mRoot( root ), mSceneManager( sceneManager ), mConfig( config )
    {
    }

    BlockSelectionRenderer::~BlockSelectionRenderer() { shutdown(); }

    bool BlockSelectionRenderer::initialize()
    {
        if( !mConfig.enabled )
            return true;
        if( !mRoot || !mSceneManager )
            return false;

        try
        {
            if( !createMaterial() )
                return false;
            mManual = mSceneManager->createManualObject( Ogre::SCENE_DYNAMIC );
            mManual->setCastShadows( false );
            mNode = mSceneManager->createSceneNode( Ogre::SCENE_DYNAMIC );
            mSceneManager->getRootSceneNode()->addChild( mNode );
            mNode->attachObject( mManual );
            createGeometry();
            mNode->setVisible( false );
            core::logInfo( "Block selection outline initialized" );
            return true;
        }
        catch( const Ogre::Exception &error )
        {
            core::logError( std::string( "Block selection renderer init failed: " ) +
                            error.getFullDescription() );
            shutdown();
            return false;
        }
    }

    bool BlockSelectionRenderer::createMaterial()
    {
        Ogre::HlmsUnlit *hlmsUnlit = static_cast<Ogre::HlmsUnlit *>(
            mRoot->getHlmsManager()->getHlms( Ogre::HLMS_UNLIT ) );
        if( !hlmsUnlit )
        {
            core::logError( "Block selection renderer requires HLMS Unlit" );
            return false;
        }

        Ogre::HlmsMacroblock macroblock;
        macroblock.mDepthCheck = mConfig.depthTest;
        macroblock.mDepthWrite = false;

        Ogre::HlmsBlendblock blendblock;
        blendblock.setBlendType( Ogre::SBT_TRANSPARENT_ALPHA );

        mDatablock = static_cast<Ogre::HlmsUnlitDatablock *>(
            hlmsUnlit->createDatablock( Ogre::IdString( mMaterialName ), mMaterialName,
                                        macroblock, blendblock, Ogre::HlmsParamVec() ) );
        if( !mDatablock )
            return false;

        mDatablock->setUseColour( true );
        mDatablock->setColour( Ogre::ColourValue(
            srgbToLinear( mConfig.color.r ), srgbToLinear( mConfig.color.g ),
            srgbToLinear( mConfig.color.b ), mConfig.color.a ) );
        return true;
    }

    void BlockSelectionRenderer::createGeometry()
    {
        if( !mManual )
            return;

        mManual->clear();
        const float e = mConfig.expand;
        const float t = mConfig.thickness;
        const float lo = -e;
        const float hi = 1.0f + e;
        const float innerLo = lo + t;
        const float innerHi = hi - t;

        constexpr std::size_t EDGE_COUNT = 12u;
        constexpr std::size_t VERTICES_PER_EDGE = 8u;
        constexpr std::size_t INDICES_PER_EDGE = 36u;
        mManual->estimateVertexCount( EDGE_COUNT * VERTICES_PER_EDGE );
        mManual->estimateIndexCount( EDGE_COUNT * INDICES_PER_EDGE );
        mManual->begin( mMaterialName, Ogre::OT_TRIANGLE_LIST );
        Ogre::uint32 base = 0u;

        // Four X edges.
        for( const float y : { lo, innerHi } )
            for( const float z : { lo, innerHi } )
                addBox( mManual, lo, y, z, hi, y + t, z + t, base );
        // Four Y edges. Insets avoid unnecessary overlap with the X beams.
        for( const float x : { lo, innerHi } )
            for( const float z : { lo, innerHi } )
                addBox( mManual, x, innerLo, z, x + t, innerHi, z + t, base );
        // Four Z edges. Insets on both other axes keep corner overdraw small.
        for( const float x : { lo, innerHi } )
            for( const float y : { lo, innerHi } )
                addBox( mManual, x, y, innerLo, x + t, y + t, innerHi, base );

        mManual->end();
        const float center = 0.5f;
        const float half = 0.5f + e + t;
        mManual->setLocalAabb( Ogre::Aabb( Ogre::Vector3( center ), Ogre::Vector3( half ) ) );
    }

    void BlockSelectionRenderer::setRenderAnchor( const world::GroupAddress &group )
    {
        if( group == mRenderAnchor )
            return;
        mRenderAnchor = group;
        updatePosition();
    }

    void BlockSelectionRenderer::setSelection(
        const std::optional<world::BlockAddress> &block )
    {
        if( block == mSelection )
            return;
        mSelection = block;
        updatePosition();
    }

    void BlockSelectionRenderer::updatePosition()
    {
        if( !mNode )
            return;
        if( !mConfig.enabled || !mSelection )
        {
            mNode->setVisible( false );
            return;
        }

        world::RelativeI64 relative{};
        const world::BlockAddress renderOrigin{ { mRenderAnchor, {} }, {} };
        constexpr std::int64_t MAX_LOCAL = 16'384;
        if( !world::blockDeltaWithin( *mSelection, renderOrigin, MAX_LOCAL, relative ) )
        {
            mNode->setVisible( false );
            return;
        }

        mNode->setPosition( static_cast<float>( relative.x ),
                            static_cast<float>( relative.y ),
                            static_cast<float>( relative.z ) );
        mNode->setVisible( true );
    }

    void BlockSelectionRenderer::shutdown()
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
                hlmsUnlit->destroyDatablock( Ogre::IdString( mMaterialName ) );
            mDatablock = nullptr;
        }
        mSelection.reset();
    }
} // namespace render
