#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace debug
{
    struct BiomeWeightLine { std::string id; std::string displayName; double weight = 0.0; };
    struct DebugHudSnapshot
    {
        float latestFps=0.0f,averageFps=0.0f,latestFrameMs=0.0f,averageFrameMs=0.0f;
        float renderLocalX=0.0f,renderLocalY=0.0f,renderLocalZ=0.0f;
        float dynamicLocalX=0.0f,dynamicLocalY=0.0f,dynamicLocalZ=0.0f;
        std::int64_t dynamicEdgeBlocks=0;
        float fractionX=0.0f,fractionY=0.0f,fractionZ=0.0f;
        std::int64_t sectorX=0,sectorY=0,sectorZ=0;
        std::int64_t regionX=0,regionY=0,regionZ=0;
        std::int64_t sectionX=0,sectionY=0,sectionZ=0;
        std::int64_t groupX=0,groupY=0,groupZ=0;
        std::int64_t chunkX=0,chunkY=0,chunkZ=0;
        std::int64_t blockX=0,blockY=0,blockZ=0;
        std::int64_t renderSectorX=0,renderSectorY=0,renderSectorZ=0;
        std::int64_t renderRegionX=0,renderRegionY=0,renderRegionZ=0;
        std::int64_t renderSectionX=0,renderSectionY=0,renderSectionZ=0;
        std::int64_t renderGroupX=0,renderGroupY=0,renderGroupZ=0;
        std::int64_t groupEdgeBlocks=0;
        std::vector<BiomeWeightLine> biomes;
        bool voxelLoaded=false; std::string voxelId; std::uint16_t voxelRuntimeId=0;
        bool targetPresent=false; std::string targetId,targetDisplayName,targetTexture; std::vector<std::string> targetTags;
        std::uint16_t targetRuntimeId=0; double targetDistance=0.0; bool targetSolid=false,targetOpaque=false,targetTransparent=false;
        std::int64_t targetSectorX=0,targetSectorY=0,targetSectorZ=0;
        std::int64_t targetRegionX=0,targetRegionY=0,targetRegionZ=0;
        std::int64_t targetSectionX=0,targetSectionY=0,targetSectionZ=0;
        std::int64_t targetGroupX=0,targetGroupY=0,targetGroupZ=0;
        std::int64_t targetChunkX=0,targetChunkY=0,targetChunkZ=0;
        std::int64_t targetBlockX=0,targetBlockY=0,targetBlockZ=0;
        std::size_t loadedChunks=0,loadedGroups=0; std::int64_t streamingRadius=0;
        std::size_t generatedChunks=0,evictedChunks=0,queuedChunks=0,readyChunks=0;
        double yawDegrees=0.0,pitchDegrees=0.0; bool flashlightEnabled=false;
    };
    std::string formatDebugHud( const DebugHudSnapshot &snapshot );
} // namespace debug
