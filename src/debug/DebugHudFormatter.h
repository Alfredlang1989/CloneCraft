#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace debug
{
    struct BiomeWeightLine
    {
        std::string id;
        std::string displayName;
        double weight = 0.0;
    };

    /** Renderer-independent facts shown by the F5 HUD. */
    struct DebugHudSnapshot
    {
        float latestFps = 0.0f;
        float averageFps = 0.0f;
        float latestFrameMs = 0.0f;
        float averageFrameMs = 0.0f;

        float renderLocalX = 0.0f;
        float renderLocalY = 0.0f;
        float renderLocalZ = 0.0f;
        float fractionX = 0.0f;
        float fractionY = 0.0f;
        float fractionZ = 0.0f;

        std::int64_t sectorX = 0, sectorY = 0, sectorZ = 0;
        std::int64_t regionX = 0, regionY = 0, regionZ = 0;
        std::int64_t groupX = 0, groupY = 0, groupZ = 0;
        std::int64_t chunkX = 0, chunkY = 0, chunkZ = 0;
        std::int64_t blockX = 0, blockY = 0, blockZ = 0;

        // Temporary v16 comparison aid. These are decimal strings because a flattened
        // coordinate can exceed int64 once Sector participates in the mixed radix.
        std::string comparisonGlobalX;
        std::string comparisonGlobalY;
        std::string comparisonGlobalZ;

        std::int64_t renderSectorX = 0, renderSectorY = 0, renderSectorZ = 0;
        std::int64_t renderRegionX = 0, renderRegionY = 0, renderRegionZ = 0;
        std::int64_t renderGroupX = 0, renderGroupY = 0, renderGroupZ = 0;
        std::int64_t groupEdgeBlocks = 0;

        std::vector<BiomeWeightLine> biomes;
        bool voxelLoaded = false;
        std::string voxelId;
        std::uint16_t voxelRuntimeId = 0;

        bool targetPresent = false;
        std::string targetId;
        std::string targetDisplayName;
        std::string targetTexture;
        std::vector<std::string> targetTags;
        std::uint16_t targetRuntimeId = 0;
        double targetDistance = 0.0;
        bool targetSolid = false;
        bool targetOpaque = false;
        bool targetTransparent = false;
        std::int64_t targetSectorX = 0, targetSectorY = 0, targetSectorZ = 0;
        std::int64_t targetRegionX = 0, targetRegionY = 0, targetRegionZ = 0;
        std::int64_t targetGroupX = 0, targetGroupY = 0, targetGroupZ = 0;
        std::int64_t targetChunkX = 0, targetChunkY = 0, targetChunkZ = 0;
        std::int64_t targetBlockX = 0, targetBlockY = 0, targetBlockZ = 0;
        std::string targetComparisonGlobalX;
        std::string targetComparisonGlobalY;
        std::string targetComparisonGlobalZ;

        std::size_t loadedChunks = 0;
        std::size_t loadedGroups = 0;
        std::int64_t streamingRadius = 0;
        std::size_t generatedChunks = 0;
        std::size_t evictedChunks = 0;
        std::size_t queuedChunks = 0;
        std::size_t readyChunks = 0;

        double yawDegrees = 0.0;
        double pitchDegrees = 0.0;
        bool flashlightEnabled = false;
    };

    std::string formatDebugHud( const DebugHudSnapshot &snapshot );
} // namespace debug
