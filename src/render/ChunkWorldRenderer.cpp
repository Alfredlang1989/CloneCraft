#include "render/ChunkWorldRenderer.h"

#include "core/Logging.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMeshBuilder.h"

#include <Math/Simple/OgreAabb.h>
#include <OgreBlendMode.h>
#include <OgreColourValue.h>
#include <OgreException.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsSamplerblock.h>
#include <OgreImage2.h>
#include <OgreManualObject2.h>
#include <OgrePixelFormatGpu.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <Hlms/Pbs/OgreHlmsPbs.h>
#include <Hlms/Pbs/OgreHlmsPbsDatablock.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace render
{
    namespace
    {
        constexpr const char *MATERIAL_PREFIX = "Omnigrid/Block/";
        constexpr const char *GENERATED_TEXTURE_PREFIX = "Omnigrid/Generated/";
        constexpr const char *RESOURCE_GROUP = "General";
        constexpr std::uint32_t GENERATED_TEXTURE_SIZE = 16u;

        std::string safeResourceName( const std::string &id )
        {
            std::string result = id;
            for( char &c : result )
            {
                const unsigned char uc = static_cast<unsigned char>( c );
                if( !std::isalnum( uc ) && c != '_' && c != '-' )
                    c = '_';
            }
            return result;
        }

        float srgbByteToLinear( std::uint8_t value )
        {
            const float srgb = static_cast<float>( value ) / 255.0f;
            if( srgb <= 0.04045f )
                return srgb / 12.92f;
            return std::pow( ( srgb + 0.055f ) / 1.055f, 2.4f );
        }

        Ogre::ColourValue jsonColorToLinear( const world::Rgba8 &color )
        {
            return Ogre::ColourValue( srgbByteToLinear( color.r ),
                                      srgbByteToLinear( color.g ),
                                      srgbByteToLinear( color.b ),
                                      static_cast<float>( color.a ) / 255.0f );
        }

        std::string describeColor( const world::Rgba8 &color, const Ogre::ColourValue &linear )
        {
            std::ostringstream out;
            out << "#" << std::uppercase << std::hex << std::setfill( '0' )
                << std::setw( 2 ) << static_cast<unsigned>( color.r )
                << std::setw( 2 ) << static_cast<unsigned>( color.g )
                << std::setw( 2 ) << static_cast<unsigned>( color.b )
                << std::dec << std::fixed << std::setprecision( 3 )
                << " -> linear(" << linear.r << ", " << linear.g << ", " << linear.b << ")";
            return out.str();
        }

        void fillTestTexture( std::vector<std::uint8_t> &pixels )
        {
            // Loud on purpose: a missing block visual must be obvious in a
            // screenshot. This is the only hardcoded visual fallback.
            pixels.resize( static_cast<std::size_t>( GENERATED_TEXTURE_SIZE ) *
                           GENERATED_TEXTURE_SIZE * 4u );
            for( std::uint32_t y = 0; y < GENERATED_TEXTURE_SIZE; ++y )
            {
                for( std::uint32_t x = 0; x < GENERATED_TEXTURE_SIZE; ++x )
                {
                    const bool magenta = ( ( x / 4u ) + ( y / 4u ) ) % 2u == 0u;
                    const std::size_t i =
                        ( static_cast<std::size_t>( y ) * GENERATED_TEXTURE_SIZE + x ) * 4u;
                    pixels[i + 0] = magenta ? 255u : 20u;
                    pixels[i + 1] = magenta ? 0u : 20u;
                    pixels[i + 2] = magenta ? 255u : 20u;
                    pixels[i + 3] = 255u;
                }
            }
        }
    } // namespace

    ChunkWorldRenderer::ChunkWorldRenderer( Ogre::Root *root,
                                            Ogre::SceneManager *sceneManager,
                                            const world::BlockRegistry &blocks,
                                            const world::BlockIdTable &table,
                                            std::filesystem::path dataDirectory )
        : mRoot( root ), mSceneManager( sceneManager ), mBlocks( blocks ), mTable( table ),
          mDataDirectory( std::move( dataDirectory ) )
    {
    }

    ChunkWorldRenderer::~ChunkWorldRenderer() { shutdown(); }

    bool ChunkWorldRenderer::initialize()
    {
        if( !mRoot || !mSceneManager )
        {
            core::logError( "ChunkWorldRenderer::initialize: no Root/SceneManager" );
            return false;
        }

        try
        {
            std::error_code fsError;
            const bool dataDirExists = !mDataDirectory.empty() &&
                                       std::filesystem::exists( mDataDirectory, fsError );
            if( fsError )
            {
                core::logWarn( "Could not inspect block texture data directory '" +
                               mDataDirectory.string() + "': " + fsError.message() );
            }
            else if( dataDirExists )
            {
                // Texture paths from blocks.json are relative to this data root.
                // Reusing General avoids creating another global resource group.
                Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
                    mDataDirectory.string(), "FileSystem", RESOURCE_GROUP, /*recursive*/ true );
            }
        }
        catch( const Ogre::Exception &e )
        {
            core::logWarn( std::string( "Could not add block texture data directory: " ) +
                           e.getFullDescription() );
        }

        if( !createBlockMaterials() )
            return false;

        mMeshBuilder = std::make_unique<world::ChunkMeshBuilder>( mTable, mBlocks );

        core::logInfo( "ChunkWorldRenderer initialized (" +
                       std::to_string( mMaterials.size() ) + " block materials)" );
        return true;
    }

    bool ChunkWorldRenderer::createBlockMaterials()
    {
        mCastShadowsByBlock.assign( mTable.size(), 1u );
        if( !mCastShadowsByBlock.empty() )
            mCastShadowsByBlock[0] = 0u;

        for( const std::string &id : mBlocks.ids() )
        {
            if( id == "core:air" )
                continue;

            const std::uint16_t blockId = mTable.indexOf( id );
            if( blockId == 0 )
                continue;
            const world::BlockDef &def = mBlocks.get( id );
            mCastShadowsByBlock[blockId] = def.castShadows ? 1u : 0u;
            if( !createBlockMaterial( blockId, def ) )
                return false;
        }
        if( mMaterials.empty() )
        {
            // No content root: the core must still start, just with nothing
            // to render. Callers render an empty world instead of aborting.
            core::logWarn( "No block materials created (empty block registry); "
                           "starting with an empty world" );
            return true;
        }
        return true;
    }

    Ogre::TextureGpu *ChunkWorldRenderer::loadConfiguredTexture( const world::BlockDef &def,
                                                                   const std::string &configuredPath,
                                                                   bool cubemap,
                                                                   bool srgb,
                                                                   const char *role )
    {
        if( configuredPath.empty() )
            return nullptr;

        const std::filesystem::path configured( configuredPath );
        const std::filesystem::path diskPath =
            configured.is_absolute() ? configured : ( mDataDirectory / configured );
        std::error_code fsError;
        const bool textureExists = std::filesystem::exists( diskPath, fsError );
        if( fsError || !textureExists )
        {
            const std::string reason = fsError ? ( " (" + fsError.message() + ")" ) : "";
            core::logWarn( "Block '" + def.id + "': " + role + " '" + diskPath.string() +
                           "' does not exist or cannot be inspected" + reason );
            return nullptr;
        }

        try
        {
            Ogre::ResourceGroupManager &resources = Ogre::ResourceGroupManager::getSingleton();

            // Resolve the configured path on disk first, then register exactly the
            // directory that owns the file. This avoids relying on recursive archive
            // name semantics for paths such as "textures/oak_leaves.png".
            const std::filesystem::path resourceDirectory = diskPath.parent_path().lexically_normal();
            const std::string resourceDirectoryText = resourceDirectory.string();
            if( mTextureResourceDirectories.insert( resourceDirectoryText ).second )
            {
                resources.addResourceLocation( resourceDirectoryText, "FileSystem", RESOURCE_GROUP,
                                               /*recursive*/ false );
            }
            const std::string resourceName = diskPath.filename().string();

            Ogre::TextureGpuManager *textureMgr =
                mRoot->getRenderSystem()->getTextureGpuManager();

            // Use OgreNext's intended file-texture presets. In particular, regular
            // diffuse/normal textures need AutomaticBatching so HLMS datablocks are
            // notified when the streaming system swaps the temporary dummy texture
            // for the fully resident texture. The CommonTextureTypes overload also
            // configures the appropriate mipmap and sRGB/normal-map filters.
            const Ogre::CommonTextureTypes::CommonTextureTypes textureType = cubemap
                ? Ogre::CommonTextureTypes::EnvMap
                : ( srgb ? Ogre::CommonTextureTypes::Diffuse
                         : Ogre::CommonTextureTypes::NormalMap );
            const std::string aliasName = std::string( "Omnigrid/File/" ) +
                                          safeResourceName( def.id ) + "/" +
                                          safeResourceName( role );
            Ogre::TextureGpu *texture = textureMgr->createOrRetrieveTexture(
                resourceName, aliasName, Ogre::GpuPageOutStrategy::Discard, textureType,
                RESOURCE_GROUP, /*poolId*/ 0u );

            // PBS 2D file textures are expected to use Ogre's automatic batching
            // path. If this invariant ever changes upstream, fail visibly here
            // instead of silently rendering the 4x4 blank/dummy texture as coal.
            if( !cubemap && !texture->hasAutomaticBatching() )
            {
                core::logError( "Block '" + def.id + "': " + role +
                                " was created without Ogre AutomaticBatching" );
                return nullptr;
            }

            texture->scheduleTransitionTo( Ogre::GpuResidency::Resident );

            std::ostringstream audit;
            audit << "Block '" << def.id << "': using " << role << " " << diskPath.string()
                  << " [resource='" << resourceName << "', realResource='"
                  << texture->getRealResourceNameStr() << "', automaticBatching="
                  << ( texture->hasAutomaticBatching() ? "yes" : "no" )
                  << ", sRGB=" << ( texture->prefersLoadingFromFileAsSRGB() ? "yes" : "no" )
                  << "]";
            core::logInfo( audit.str() );
            return texture;
        }
        catch( const Ogre::Exception &e )
        {
            core::logWarn( "Block '" + def.id + "': Ogre failed to load " + role + " '" +
                           diskPath.string() + "': " + e.getFullDescription() );
            return nullptr;
        }
    }

    Ogre::TextureGpu *ChunkWorldRenderer::createGeneratedTexture( std::uint16_t blockId,
                                                                  const world::BlockDef &def )
    {
        // Generated GPU textures are reserved for the diagnostic fallback.
        // JSON solid colours are fed directly into the PBS datablock instead of
        // being round-tripped through an asynchronous 16x16 texture. Besides
        // being cheaper, this makes the colour path independent from texture
        // streaming residency and is much easier to audit.
        std::vector<std::uint8_t> pixels;
        fillTestTexture( pixels );
        core::logWarn( "Block '" + def.id +
                       "': no texture/color configured; using built-in test texture" );

        Ogre::TextureGpuManager *textureMgr = mRoot->getRenderSystem()->getTextureGpuManager();
        const std::string textureName = std::string( GENERATED_TEXTURE_PREFIX ) +
                                        safeResourceName( def.id ) + "_" +
                                        std::to_string( blockId );
        Ogre::TextureGpu *texture = textureMgr->createTexture(
            textureName, textureName, Ogre::GpuPageOutStrategy::Discard,
            static_cast<Ogre::uint32>( Ogre::TextureFlags::ManualTexture ),
            Ogre::TextureTypes::Type2D, RESOURCE_GROUP,
            /*filters*/ 0u, /*poolId*/ 0u );
        texture->setResolution( GENERATED_TEXTURE_SIZE, GENERATED_TEXTURE_SIZE );
        texture->setPixelFormat( Ogre::PFG_RGBA8_UNORM_SRGB );

        auto *image = new Ogre::Image2;
        image->createEmptyImage( GENERATED_TEXTURE_SIZE, GENERATED_TEXTURE_SIZE, 1,
                                 Ogre::TextureTypes::Type2D, Ogre::PFG_RGBA8_UNORM_SRGB, 1u );
        const Ogre::TextureBox box = image->getData( 0 );
        std::memcpy( box.data, pixels.data(), pixels.size() );
        texture->scheduleTransitionTo( Ogre::GpuResidency::Resident, image, true );
        return texture;
    }

    bool ChunkWorldRenderer::createBlockMaterial( std::uint16_t blockId,
                                                   const world::BlockDef &def )
    {
        Ogre::TextureGpu *diffuseTexture = nullptr;
        bool ownsDiffuseTexture = false;
        bool useJsonColor = false;

        // Required visual priority: texture -> JSON colour -> diagnostic texture.
        // A JSON colour is represented as PBS background diffuse directly. It
        // must NOT depend on a generated texture becoming resident.
        if( !def.texture.empty() )
            diffuseTexture = loadConfiguredTexture( def, def.texture, false, true, "diffuse texture" );
        if( !diffuseTexture && def.color.has_value() )
        {
            useJsonColor = true;
        }
        else if( !diffuseTexture )
        {
            diffuseTexture = createGeneratedTexture( blockId, def );
            ownsDiffuseTexture = diffuseTexture != nullptr;
            if( !diffuseTexture )
                return false;
        }

        Ogre::TextureGpu *normalTexture = nullptr;
        if( !def.normalMap.empty() )
            normalTexture = loadConfiguredTexture( def, def.normalMap, false, false, "normal map" );

        Ogre::TextureGpu *reflectionTexture = nullptr;
        if( !def.reflectionMap.empty() )
            reflectionTexture = loadConfiguredTexture( def, def.reflectionMap, true, false, "reflection cubemap" );

        Ogre::HlmsManager *hlmsManager = mRoot->getHlmsManager();
        Ogre::HlmsPbs *hlmsPbs =
            static_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) );
        if( !hlmsPbs )
        {
            core::logError( "ChunkWorldRenderer: no HLMS PBS available" );
            if( ownsDiffuseTexture )
                mRoot->getRenderSystem()->getTextureGpuManager()->destroyTexture( diffuseTexture );
            return false;
        }

        const std::string materialName = std::string( MATERIAL_PREFIX ) + safeResourceName( def.id );
        Ogre::HlmsMacroblock macroblock;
        Ogre::HlmsBlendblock blendblock;

        Ogre::HlmsPbsDatablock *datablock =
            static_cast<Ogre::HlmsPbsDatablock *>( hlmsPbs->createDatablock(
                Ogre::IdString( materialName ), materialName, macroblock, blendblock,
                Ogre::HlmsParamVec() ) );
        if( !datablock )
        {
            core::logError( "ChunkWorldRenderer: PBS datablock creation failed for " + def.id );
            if( ownsDiffuseTexture )
                mRoot->getRenderSystem()->getTextureGpuManager()->destroyTexture( diffuseTexture );
            return false;
        }

        Ogre::HlmsSamplerblock samplerblock;
        samplerblock.setAddressingMode( Ogre::TextureAddressingMode::TAM_WRAP );

        // Keep the PBS diffuse multiplier explicitly neutral. Ogre stores this
        // internally as 1/PI for energy conservation. The actual albedo comes
        // either from the sRGB diffuse texture or from JSON backgroundDiffuse.
        datablock->setDiffuse( Ogre::Vector3::UNIT_SCALE );

        if( diffuseTexture )
        {
            // Bind the already-created TextureGpu directly. Name-based PBS binding
            // asks ResourceGroupManager to locate a file/archive resource with the
            // same name, which is wrong for generated fallback textures.
            datablock->setTexture( Ogre::PBSM_DIFFUSE, diffuseTexture, &samplerblock );
        }
        else if( useJsonColor )
        {
            const Ogre::ColourValue linearColor = jsonColorToLinear( *def.color );
            datablock->setBackgroundDiffuse( linearColor );
            core::logInfo( "Block '" + def.id + "': JSON albedo " +
                           describeColor( *def.color, linearColor ) +
                           " mapped directly to PBS background diffuse" );
        }

        // Material parameters are authoritative in blocks.json. The renderer only
        // translates them into Ogre PBS concepts.
        datablock->setRoughness( def.roughness );
        datablock->setReceiveShadows( def.receiveShadows );

        if( def.refraction > 0.0f )
        {
            // Give Ogre the optical IOR first. The explicit JSON reflection
            // value below deliberately wins afterwards so artists can tune F0
            // independently from screen-space refraction strength.
            datablock->setIndexOfRefraction(
                Ogre::Vector3( def.indexOfRefraction, def.indexOfRefraction,
                               def.indexOfRefraction ), false );
        }

        if( def.metalness > 0.0001f )
        {
            datablock->setWorkflow( Ogre::HlmsPbsDatablock::MetallicWorkflow );
            datablock->setMetalness( def.metalness );
            // In metallic workflow Ogre advises against Fresnel overrides. Use
            // reflection as a global specular strength instead.
            datablock->setSpecular( Ogre::Vector3( def.reflection, def.reflection, def.reflection ) );
        }
        else
        {
            datablock->setWorkflow( Ogre::HlmsPbsDatablock::SpecularAsFresnelWorkflow );
            datablock->setFresnel( Ogre::Vector3( def.reflection, def.reflection, def.reflection ), false );
        }

        if( normalTexture )
        {
            datablock->setTexture( Ogre::PBSM_NORMAL, normalTexture, &samplerblock );
            datablock->setNormalMapWeight( def.normalMapStrength );
        }

        if( reflectionTexture )
        {
            // Ogre requires PBSM_REFLECTION to be a cubemap. This is useful for
            // special blocks (polished metal, glass) while ordinary blocks still
            // receive sun/specular reflection from the scalar reflection value.
            datablock->setTexture( Ogre::PBSM_REFLECTION, reflectionTexture, nullptr );
        }

        if( def.emission > 0 )
        {
            const float e = static_cast<float>( def.emission ) / 15.0f;
            datablock->setEmissive( Ogre::Vector3( e, e, e ) );
        }

        if( def.alphaMode == world::BlockAlphaMode::Mask )
        {
            // Cutout vegetation belongs in the opaque/depth-writing render path.
            // The texture alpha only decides whether a fragment exists. Ogre applies
            // the same alpha test to shadow caster shaders, so leaves cast leaf-shaped
            // shadows instead of solid voxel-box shadows.
            datablock->setAlphaTest( Ogre::CMPF_GREATER_EQUAL,
                                     /*shadowCasterOnly*/ false,
                                     /*useAlphaFromTextures*/ true );
            datablock->setAlphaTestThreshold( def.alphaCutoff );
        }
        else if( def.alphaMode == world::BlockAlphaMode::Blend )
        {
            // Omnigrid JSON: 0 = opaque, 1 = fully transparent.
            // Ogre PBS: 0 = fully transparent, 1 = fully opaque.
            const float opacity = std::clamp( 1.0f - def.transparency, 0.0f, 1.0f );
            datablock->setTransparency( opacity, Ogre::HlmsPbsDatablock::Transparent,
                                        /*useAlphaFromTextures*/ true,
                                        /*changeBlendblock*/ true );
        }

        if( def.refraction > 0.0f )
        {
            // Store the physical optical parameters now. OgreNext only evaluates
            // screen-space displacement when the material is rendered in a
            // dedicated compositor pass with `use_refractions`; until that pass is
            // enabled we deliberately keep Transparent mode as a stable fallback.
            datablock->setRefractionStrength( def.refraction );
            core::logInfo( "Block '" + def.id + "': refraction configured; transparent PBS fallback active until the dedicated refraction pass is enabled" );
        }

        {
            std::ostringstream audit;
            audit << std::fixed << std::setprecision( 3 )
                  << "Material '" << def.id << "': source="
                  << ( diffuseTexture ? ( def.texture.empty() ? "diagnostic-texture" : "texture" )
                                      : "json-color" )
                  << ", roughness=" << def.roughness
                  << ", metalness=" << def.metalness
                  << ", reflection=" << def.reflection
                  << ", transparency=" << def.transparency
                  << ", alphaMode="
                  << ( def.alphaMode == world::BlockAlphaMode::Mask ? "mask"
                       : def.alphaMode == world::BlockAlphaMode::Blend ? "blend"
                                                                        : "opaque" )
                  << ", alphaCutoff=" << def.alphaCutoff
                  << ", refraction=" << def.refraction
                  << ", normalMap=" << ( normalTexture ? "yes" : "no" );
            core::logInfo( audit.str() );
        }

        mMaterials.emplace( blockId, BlockMaterial{ materialName, diffuseTexture, normalTexture,
                                                    reflectionTexture, datablock,
                                                    ownsDiffuseTexture,
                                                    normalTexture != nullptr } );
        return true;
    }

    const std::string *ChunkWorldRenderer::materialNameFor( std::uint16_t blockId ) const
    {
        const auto it = mMaterials.find( blockId );
        return it == mMaterials.end() ? nullptr : &it->second.materialName;
    }

    bool ChunkWorldRenderer::meshContainsShadowClass( const world::ChunkMesh &mesh,
                                                         bool castShadows ) const
    {
        for( std::size_t base = 0; base + 3u < mesh.vertices.size(); base += 4u )
        {
            const std::uint16_t blockId = mesh.vertices[base].blockId;
            if( blockId == 0 || blockId >= mTable.size() )
                continue;
            if( blockId < mCastShadowsByBlock.size() &&
                ( mCastShadowsByBlock[blockId] != 0u ) == castShadows )
                return true;
        }
        return false;
    }

    void ChunkWorldRenderer::positionChunkNode( const world::ChunkAddress &chunk,
                                                   Ogre::SceneNode *node ) const
    {
        if( !node )
            return;

        const world::RelativeI64 relative =
            world::chunkOriginRelativeToGroup( chunk, mRenderAnchor );

        // Loaded terrain should always remain close to the camera's integer
        // render anchor. If this fires, fail loudly rather than silently feed
        // low-precision megacoordinates back into Ogre.
        constexpr std::int64_t kMaxLocalRenderDistance = 16'384;
        const auto outsideLocalFrame = [kMaxLocalRenderDistance]( std::int64_t value ) {
            return value < -kMaxLocalRenderDistance || value > kMaxLocalRenderDistance;
        };
        if( outsideLocalFrame( relative.x ) || outsideLocalFrame( relative.y ) ||
            outsideLocalFrame( relative.z ) )
        {
            throw std::runtime_error(
                "ChunkWorldRenderer local render offset exceeded 16384 blocks; "
                "render-anchor invariant broken" );
        }

        node->setPosition( static_cast<float>( relative.x ),
                           static_cast<float>( relative.y ),
                           static_cast<float>( relative.z ) );
    }

    void ChunkWorldRenderer::setRenderAnchor( const world::GroupAddress &group )
    {
        if( group == mRenderAnchor )
            return;

        mRenderAnchor = group;
        for( auto &[chunk, object] : mObjects )
            positionChunkNode( chunk, object.node );
    }

    void ChunkWorldRenderer::sync( world::ChunkManager &chunks )
    {
        if( mMaterials.empty() )
            return;

        if( !mListening )
        {
            chunks.setOnChunkChange( [this]( const world::ChunkAddress &c ) { mDirty.insert( c ); } );
            mObservedChunks = &chunks;
            mListening = true;
            chunks.forEachChunk( [this]( const world::Chunk &chunk ) {
                mDirty.insert( chunk.coord() );
            } );
        }

        if( mDirty.empty() )
            return;

        if( !mMeshBuilder )
            mMeshBuilder = std::make_unique<world::ChunkMeshBuilder>( mTable, mBlocks );

        const auto destroyManual = [this]( Ogre::ManualObject *&manual ) {
            if( manual )
            {
                mSceneManager->destroyManualObject( manual );
                manual = nullptr;
            }
        };

        // Do not turn a burst of newly committed chunks into a second frame
        // spike. Unloaded objects are removed immediately (cheap), while CPU
        // meshing + Ogre uploads are spread over a small per-frame budget.
        std::set<world::ChunkAddress> pending;
        pending.swap( mDirty );
        std::size_t meshBudget = 6u;

        for( const world::ChunkAddress &c : pending )
        {
            auto *chunk = chunks.chunkAt( c );
            auto objIt = mObjects.find( c );

            if( !chunk )
            {
                if( objIt != mObjects.end() )
                {
                    destroyManual( objIt->second.shadowCaster );
                    destroyManual( objIt->second.noShadowCaster );
                    mSceneManager->destroySceneNode( objIt->second.node );
                    mObjects.erase( objIt );
                }
                continue;
            }

            if( meshBudget == 0u )
            {
                mDirty.insert( c );
                continue;
            }
            --meshBudget;

            mMeshBuilder->build( chunks, c, mMeshScratch );
            const world::ChunkMesh &mesh = mMeshScratch;
            if( mesh.vertices.empty() )
            {
                if( objIt != mObjects.end() )
                {
                    destroyManual( objIt->second.shadowCaster );
                    destroyManual( objIt->second.noShadowCaster );
                    mSceneManager->destroySceneNode( objIt->second.node );
                    mObjects.erase( objIt );
                }
                continue;
            }

            if( objIt == mObjects.end() )
            {
                auto *node = mSceneManager->createSceneNode( Ogre::SCENE_DYNAMIC );
                positionChunkNode( c, node );
                mSceneManager->getRootSceneNode()->addChild( node );
                objIt = mObjects.emplace( c, ChunkObject{ node, nullptr, nullptr } ).first;
            }

            ChunkObject &obj = objIt->second;
            const Ogre::Aabb aabb( Ogre::Vector3( world::BLOCKS_PER_CHUNK_EDGE / 2.0f ),
                                   Ogre::Vector3( world::BLOCKS_PER_CHUNK_EDGE / 2.0f ) );

            const auto updateClass = [&]( bool casts, Ogre::ManualObject *&manual ) {
                if( !meshContainsShadowClass( mesh, casts ) )
                {
                    destroyManual( manual );
                    return;
                }
                if( !manual )
                {
                    manual = mSceneManager->createManualObject( Ogre::SCENE_DYNAMIC );
                    manual->setCastShadows( casts );
                    manual->setLocalAabb( aabb );
                    obj.node->attachObject( manual );
                }
                rebuildManualObject( manual, mesh, casts );
            };

            // Two objects at most per chunk lets JSON castShadows be honoured
            // without exploding to one Ogre object per material/block type.
            updateClass( true, obj.shadowCaster );
            updateClass( false, obj.noShadowCaster );
        }

    }

    void ChunkWorldRenderer::rebuildManualObject( Ogre::ManualObject *manual,
                                                   const world::ChunkMesh &mesh,
                                                   bool castShadows )
    {
        manual->clear();

        if( mesh.vertices.size() % 4u != 0u )
        {
            core::logError( "Chunk mesh invariant broken: vertex count is not divisible by 4" );
            return;
        }

        // Greedy builder emits four unique vertices per quad. Group quads
        // by block id so each Ogre ManualObject section uses exactly one
        // repeatable block texture/material.
        // Runtime block ids are dense, so a vector is both simpler and faster
        // than a tree-map plus node allocations for every chunk rebuild.
        std::vector<std::vector<std::size_t>> quadsByBlock( mTable.size() );
        for( std::size_t base = 0; base < mesh.vertices.size(); base += 4u )
        {
            const std::uint16_t blockId = mesh.vertices[base].blockId;
            if( blockId == 0 )
                continue;
            if( blockId >= quadsByBlock.size() )
            {
                core::logError( "Chunk mesh contains invalid runtime block id " +
                                std::to_string( blockId ) );
                continue;
            }
            if( ( mCastShadowsByBlock[blockId] != 0u ) != castShadows )
                continue;
            bool consistent = true;
            for( std::size_t i = 1; i < 4u; ++i )
                consistent = consistent && mesh.vertices[base + i].blockId == blockId;
            if( !consistent )
            {
                core::logError( "Chunk mesh invariant broken: one quad contains mixed block ids" );
                continue;
            }
            quadsByBlock[blockId].push_back( base );
        }

        std::size_t selectedQuadCount = 0u;
        for( const auto &quads : quadsByBlock )
            selectedQuadCount += quads.size();
        manual->estimateVertexCount( selectedQuadCount * 4u );
        manual->estimateIndexCount( selectedQuadCount * 6u );

        for( std::size_t blockIndex = 1; blockIndex < quadsByBlock.size(); ++blockIndex )
        {
            const std::uint16_t blockId = static_cast<std::uint16_t>( blockIndex );
            const auto &quadBases = quadsByBlock[blockIndex];
            if( quadBases.empty() )
                continue;

            const std::string *materialName = materialNameFor( blockId );
            if( !materialName )
            {
                core::logError( "No material for runtime block id " + std::to_string( blockId ) );
                continue;
            }

            const auto materialIt = mMaterials.find( blockId );
            const bool usesNormalMap = materialIt != mMaterials.end() &&
                                       materialIt->second.usesNormalMap;

            manual->begin( *materialName, Ogre::OT_TRIANGLE_LIST );
            std::uint32_t localBase = 0;
            for( const std::size_t base : quadBases )
            {
                for( std::size_t i = 0; i < 4u; ++i )
                {
                    const world::MeshVertex &v = mesh.vertices[base + i];
                    manual->position( v.x, v.y, v.z );
                    manual->normal( v.nx, v.ny, v.nz );
                    // Ogre PBS only consumes tangent input for normal-mapped
                    // materials. Avoid advertising an unnecessary vertex semantic
                    // on every terrain section; it reduces vertex bandwidth and
                    // keeps shader input layouts simpler on older/iGPU drivers.
                    if( usesNormalMap )
                        manual->tangent( v.tx, v.ty, v.tz );
                    manual->textureCoord( v.u, v.v );
                }
                manual->triangle( localBase + 0, localBase + 1, localBase + 2 );
                manual->triangle( localBase + 0, localBase + 2, localBase + 3 );
                localBase += 4u;
            }
            manual->end();
        }
    }

    void ChunkWorldRenderer::shutdown()
    {
        if( mSceneManager )
        {
            for( auto &[coord, obj] : mObjects )
            {
                (void)coord;
                if( obj.shadowCaster )
                    mSceneManager->destroyManualObject( obj.shadowCaster );
                if( obj.noShadowCaster )
                    mSceneManager->destroyManualObject( obj.noShadowCaster );
                if( obj.node )
                    mSceneManager->destroySceneNode( obj.node );
            }
        }
        mObjects.clear();

        if( mRoot )
        {
            Ogre::HlmsPbs *hlmsPbs = static_cast<Ogre::HlmsPbs *>(
                mRoot->getHlmsManager()->getHlms( Ogre::HLMS_PBS ) );

            // Renderables are already gone, so remove our per-block datablocks
            // before destroying generated textures they reference. This also
            // makes renderer re-initialisation in the same Ogre Root safe.
            if( hlmsPbs )
            {
                for( const auto &[blockId, material] : mMaterials )
                {
                    (void)blockId;
                    if( material.datablock )
                        hlmsPbs->destroyDatablock( Ogre::IdString( material.materialName ) );
                }
            }

            Ogre::TextureGpuManager *textureMgr =
                mRoot->getRenderSystem()->getTextureGpuManager();
            std::set<Ogre::TextureGpu *> uniqueOwnedTextures;
            for( const auto &[blockId, material] : mMaterials )
            {
                (void)blockId;
                if( material.diffuseTexture && material.ownsDiffuseTexture )
                    uniqueOwnedTextures.insert( material.diffuseTexture );
            }
            for( Ogre::TextureGpu *texture : uniqueOwnedTextures )
                textureMgr->destroyTexture( texture );
        }

        // Configured file textures are manager-owned/retrieved resources;
        // generated colour/fallback textures above are ours and were freed.
        // ChunkManager stores a callback that captured this renderer. Detach
        // it before the renderer dies so a later chunk edit cannot call into
        // freed memory. Application owns ChunkManager longer than this object.
        if( mObservedChunks )
            mObservedChunks->setOnChunkChange( {} );
        mObservedChunks = nullptr;

        mMeshBuilder.reset();
        mMeshScratch.vertices.clear();
        mMeshScratch.vertices.shrink_to_fit();
        mMaterials.clear();
        mCastShadowsByBlock.clear();
        mDirty.clear();
        mListening = false;
    }
} // namespace render
