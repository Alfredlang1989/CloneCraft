#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace worldgen
{
    // ---------------------------------------------------------------------
    // Active data-driven terrain runtime.
    // ---------------------------------------------------------------------
    enum class FieldDimension : std::uint8_t
    {
        D2,
        D3
    };

    /** Scalar field evaluated by Lua. */
    struct FieldConfig
    {
        std::string id;
        FieldDimension dimension = FieldDimension::D3;
        std::filesystem::path scriptPath;
        std::string functionName = "sample";
        std::uint64_t salt = 0;
    };

    enum class PassType : std::uint8_t
    {
        FillBelow,
        SurfaceLayer,
        Surface,
        Volume
    };

    /**
     * Data-defined ordering boundary for block-mutating worldgen passes.
     * Stage names and their global order are loaded from stage.json.
     */
    struct StageConfig
    {
        std::string id;
        std::int32_t order = 0;
    };

    enum class CompareOp : std::uint8_t
    {
        Always,
        Greater,
        GreaterEqual,
        Less,
        LessEqual,
        Between
    };

    struct FieldCondition
    {
        CompareOp op = CompareOp::Greater;
        double value = 0.5;
        double maxValue = 1.0;
    };

    struct PassConfig
    {
        std::string id;
        PassType type = PassType::Volume;
        std::string stage;
        std::string blockId;
        std::int32_t priority = 0;

        std::string field;
        std::string maskField;
        std::string biome; // optional resolved biome id; ANDed with maskField when both are present
        bool biomeDominant = false; // require this biome to be the strongest resolved weight
        std::string surfaceField;
        std::string thicknessField;
        std::string bottomField;
        std::int32_t thickness = 1;
        std::int32_t surfaceOffset = 0;
        std::int32_t bottomOffset = 0;

        FieldCondition condition;
        FieldCondition maskCondition;
        FieldCondition biomeCondition{ CompareOp::Greater, 0.30, 1.0 };
        std::vector<std::string> replaceBlocks;
        std::vector<std::string> replaceTags;
    };

    // ---------------------------------------------------------------------
    // Decoration runtime. Runs after all configured block-mutation stages.
    // ---------------------------------------------------------------------
    struct AnchorConditionConfig
    {
        std::string field;
        FieldCondition condition;
    };

    enum class AnchorSurfaceMode : std::uint8_t
    {
        Field,       // y comes directly from surfaceField
        Postprocess  // snap onto the final post-mutation surface
    };

    /**
     * Deterministic immutable placement set shared by one or more decoration
     * passes. This is the thread boundary for multi-pass structures: wood and
     * leaves may execute independently while consuming exactly the same anchors.
     */
    struct AnchorSetConfig
    {
        std::string id;
        std::string surfaceField = "surface_height";
        AnchorSurfaceMode surfaceMode = AnchorSurfaceMode::Field;
        std::string densityField; // optional 2D 0..1 multiplier
        std::string biome; // optional resolved biome id used for placement eligibility/density
        FieldCondition biomeCondition{ CompareOp::Greater, 0.25, 1.0 };
        std::int32_t surfaceOffset = 0;
        std::int32_t spacing = 1; // one jittered candidate per spacing x spacing cell
        std::int32_t maxSurfaceDrop = 1024; // Postprocess search safety bound
        double chance = 1.0;
        std::uint64_t salt = 0;
        std::vector<AnchorConditionConfig> conditions;
    };

    enum class DecorationType : std::uint8_t
    {
        Scatter,   // one block at the anchor
        Column,    // vertical 1D structure, e.g. sugar cane
        Structure  // bounded Lua voxel structure using a palette
    };

    struct StructureBounds
    {
        std::int32_t minX = 0;
        std::int32_t maxX = 0;
        std::int32_t minY = 0;
        std::int32_t maxY = 0;
        std::int32_t minZ = 0;
        std::int32_t maxZ = 0;
    };

    /**
     * Post-mutation decoration proposal producer. Like mutation passes, decoration
     * passes never mutate another producer and are deterministically merged later.
     */
    struct DecorationPassConfig
    {
        std::string id;
        DecorationType type = DecorationType::Scatter;
        std::string anchorSet;
        std::int32_t priority = 200;

        // Scatter / Column
        std::string blockId;
        std::int32_t minHeight = 1;
        std::int32_t maxHeight = 1;

        // Structure. Lua returns 0 for no block or 1..palette.size().
        std::filesystem::path scriptPath;
        std::string functionName = "sample";
        std::uint64_t salt = 0;
        StructureBounds bounds;
        std::vector<std::string> palette;

        // Optional species/variant partition on the shared anchor's stable
        // random selector. Interval semantics are [anchorMin, anchorMax).
        double anchorMin = 0.0;
        double anchorMax = 1.0;

        std::vector<std::string> replaceBlocks;
        std::vector<std::string> replaceTags;
        std::vector<std::string> supportBlocks;
        std::vector<std::string> supportTags;
    };

    struct WorldGenConfig
    {
        std::uint64_t seed = 0;
        std::uint32_t workerThreads = 0;
        std::string surfaceField = "surface_height";
        std::filesystem::path stageRegistryPath;
        std::vector<StageConfig> stages;
        std::vector<FieldConfig> fields;
        std::vector<PassConfig> passes;
        std::vector<AnchorSetConfig> anchorSets;
        std::vector<DecorationPassConfig> decorations;
    };
} // namespace worldgen
