#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "world/chunk/ChunkManager.h"
#include "world/coordinates/Coords.h"
#include "world/coordinates/WorldPosition.h"
#include "world/mesh/ChunkMesh.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"

namespace Ogre
{
    class Root;
    class SceneManager;
    class SceneNode;
    class ManualObject; // NOLINT(bugprone-forward-declaration-namespace)
    class TextureGpu;
    class HlmsPbsDatablock;
}

namespace world
{
    class ChunkMeshBuilder;
}

namespace render
{
    /**
     * GPU-side representation of loaded voxel chunks.
     *
     * Important design choice: greedy meshes do NOT use a packed texture
     * atlas. Every block type gets its own Ogre texture/material section.
     * This makes UVs such as 0..16 repeat one selected block texture safely
     * with TAM_WRAP and avoids the classic "greedy quad walks through the
     * whole atlas" corruption.
     *
     * Block visuals are selected strictly from BlockDef:
     *   texture -> JSON PBS colour -> generated test texture fallback.
     */
    class ChunkWorldRenderer
    {
    public:
        ChunkWorldRenderer( Ogre::Root *root,
                            Ogre::SceneManager *sceneManager,
                            const world::BlockRegistry &blocks,
                            const world::BlockIdTable &table,
                            std::filesystem::path dataDirectory );
        ~ChunkWorldRenderer();

        bool initialize();

        /** Selects the integer ChunkGroup that defines render-space (0,0,0).
         *  All existing chunk nodes are rebased immediately when it changes. */
        void setRenderAnchor( const world::GroupAddress &group );

        void sync( world::ChunkManager &chunks );
        void shutdown();

    private:
        struct ChunkObject
        {
            Ogre::SceneNode *node = nullptr;
            Ogre::ManualObject *shadowCaster = nullptr;
            Ogre::ManualObject *noShadowCaster = nullptr;
        };

        struct BlockMaterial
        {
            std::string materialName;
            Ogre::TextureGpu *diffuseTexture = nullptr;
            Ogre::TextureGpu *normalTexture = nullptr;
            Ogre::TextureGpu *reflectionTexture = nullptr;
            Ogre::HlmsPbsDatablock *datablock = nullptr;
            bool ownsDiffuseTexture = false; // generated diagnostic fallback texture only
            bool usesNormalMap = false;
        };

        bool createBlockMaterials();
        bool createBlockMaterial( std::uint16_t blockId, const world::BlockDef &def );
        Ogre::TextureGpu *createGeneratedTexture( std::uint16_t blockId,
                                                  const world::BlockDef &def );
        Ogre::TextureGpu *loadConfiguredTexture( const world::BlockDef &def,
                                                  const std::string &configuredPath,
                                                  bool cubemap,
                                                  bool srgb,
                                                  const char *role );
        const std::string *materialNameFor( std::uint16_t blockId ) const;

        void rebuildManualObject( Ogre::ManualObject *manual,
                                  const world::ChunkMesh &mesh,
                                  bool castShadows );
        bool meshContainsShadowClass( const world::ChunkMesh &mesh, bool castShadows ) const;
        void positionChunkNode( const world::ChunkAddress &chunk, Ogre::SceneNode *node ) const;

        Ogre::Root *mRoot = nullptr;
        Ogre::SceneManager *mSceneManager = nullptr;
        const world::BlockRegistry &mBlocks;
        const world::BlockIdTable &mTable;
        std::filesystem::path mDataDirectory;
        std::set<std::string> mTextureResourceDirectories;

        std::map<std::uint16_t, BlockMaterial> mMaterials;
        std::vector<std::uint8_t> mCastShadowsByBlock;

        // Meshing is a hot streaming path. Keep registry-derived caches and
        // vector capacity alive across dirty chunks instead of reconstructing
        // both for every sync/rebuild.
        std::unique_ptr<world::ChunkMeshBuilder> mMeshBuilder;
        world::ChunkMesh mMeshScratch;
        std::map<world::ChunkAddress, ChunkObject> mObjects;
        std::set<world::ChunkAddress> mDirty;
        world::GroupAddress mRenderAnchor{};
        world::ChunkManager *mObservedChunks = nullptr;
        bool mListening = false;
    };
} // namespace render
