#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace world
{
    /**
     * Error reported by registry parsing/validation. The message always
     * mentions the source (file path or "inline"), the entry position and
     * what exactly is wrong, e.g.
     *
     *   data/blocks.json: entry 2: missing required field 'displayName'
     *   data/blocks.json: entry 3 (id=core:stone): duplicate id
     */
    class RegistryError : public std::runtime_error
    {
    public:
        explicit RegistryError( const std::string &message ) : std::runtime_error( message ) {}
    };

    /** RGBA colour stored in data files. */
    struct Rgba8
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;

        friend bool operator==( const Rgba8 &, const Rgba8 & ) = default;
    };

    enum class BlockRenderShape : std::uint8_t
    {
        Cube,
        Cross
    };

    /** How texture alpha is interpreted by the renderer. */
    enum class BlockAlphaMode : std::uint8_t
    {
        Opaque,
        Mask,  // binary/cutout alpha test; participates in normal depth sorting
        Blend  // conventional transparency / refraction path
    };

    /** Registered block definition (data-driven, never hardcoded). */
    struct BlockDef
    {
        std::string id;         // e.g. "core:stone" (namespace:id)
        std::string displayName;
        // Semantic tags are deliberately renderer-agnostic. Worldgen passes
        // use them for generic replacement rules (for example
        // "terrain:rock" or "terrain:carvable") without hard-coding block
        // ids in C++.
        std::vector<std::string> tags;
        bool solid = false;     // collider / interaction geometry?
        bool transparent = false; // legacy/transmittance flag; Blend mode renders transparent
        bool opaque = true;     // blocks voxel face occlusion
        std::int32_t emission = 0; // light emission 0..15, 0 = none
        BlockRenderShape renderShape = BlockRenderShape::Cube;
        BlockAlphaMode alphaMode = BlockAlphaMode::Opaque;
        float alphaCutoff = 0.5f; // used only by AlphaMode::Mask

        // Visual selection is deliberately data-driven and ordered:
        //   1. texture if non-empty
        //   2. colour if present
        //   3. renderer-generated test/fallback texture
        // Texture paths are relative to the configured data directory.
        std::string texture;
        std::optional<Rgba8> color;

        // Physically based material properties. These are deliberately kept
        // renderer-agnostic in the registry so mods/data files own the look.
        // JSON convention: transparency 0 = opaque, 1 = fully transparent.
        std::string normalMap;
        std::string reflectionMap; // optional cubemap / environment map
        float roughness = 0.85f;
        float metalness = 0.0f;
        float reflection = 0.04f;  // dielectric F0 / fresnel reflectance
        float transparency = 0.0f;
        float refraction = 0.0f;   // screen-space refraction strength
        float indexOfRefraction = 1.45f;
        float normalMapStrength = 1.0f;
        bool receiveShadows = true;
        bool castShadows = true;
    };

    /** Data-driven terrain profile blended by continuous biome weights. */
    struct BiomeTerrainDef
    {
        double heightOffset = 0.0;
        double heightMultiplier = 1.0;
        double detailAmplitude = 3.0;
        double detailScale = 0.018;
        double detailMultiplier = 1.0;

        // Ridged noise is especially useful for alpine/high-mountain terrain.
        double ridgeAmplitude = 0.0;
        double ridgeScale = 0.003;
        double ridgeSharpness = 3.0;

        // Optional sparse island uplift, normally enabled only for ocean biomes.
        double islandAmplitude = 0.0;
        double islandScale = 0.0075;
        double islandThreshold = 0.78;
        double islandSharpness = 2.0;
    };

    /** Smooth data-driven selector over a shared 2D worldgen field.
     * Values inside [minValue,maxValue] have full suitability. `fade` controls
     * the smooth falloff outside that interval. */
    struct BiomeSelectionFieldDef
    {
        std::string field;
        double minValue = 0.0;
        double maxValue = 1.0;
        double fade = 0.0;
    };

    /** Registered biome definition. */
    struct BiomeDef
    {
        std::string id;         // e.g. "core:plains"
        std::string displayName;
        std::string surfaceBlock; // block id used as top layer
        std::string fillerBlock;  // block id used below surface
        std::string resourceId;   // optional resource deposit to place
        double temperature = 0.5; // 0..1 climate target
        double rainfall = 0.5;    // 0..1 climate target
        double continentalness = 0.5; // 0=oceanic, 1=deep continental/highland
        double weight = 1.0;      // relative competition weight in worldgen
        double selectionSharpness = 1.0; // >1 makes strong suitability win more decisively
        std::vector<BiomeSelectionFieldDef> selectionFields;
        // Optional 2D worldgen field (0..1) for regional modulation only.
        // Climate/geology eligibility belongs in selectionFields so shared fields
        // are sampled once and reused by every biome.
        // Optional 2D worldgen field (0..1) controlling where this biome
        // contributes its terrain profile. Empty means fallback/default biome.
        std::string terrainMaskField;
        BiomeTerrainDef terrain;
    };

    /** Registered resource (feature to be scattered by worldgen). */
    struct ResourceDef
    {
        std::string id;         // e.g. "core:coal"
        std::string displayName;
        std::string blockId;    // block that is placed
        double weight = 1.0;    // relative choice weight among eligible resources
        double chance = 1.0;    // probability 0..1 that the chosen column attempt is placed
        std::int32_t minY = 1;  // world height range
        std::int32_t maxY = 64;
    };

    /**
     * Registered gameplay object/block prototype (e.g. "default:cactus").
     *
     * Prototypes are the *logical* identity layer on top of physical block
     * ids: a voxel stores a compact block index, while gameplay refers to a
     * stable namespaced prototype id. Several prototypes may share one
     * block, and a prototype may later own sidecar data (M04+) and
     * event/action hooks (M07).
     *
     * Ids are always namespaced (<namespace>:<name>, both non-empty) and
     * never depend on load order. The runtime handle (PrototypeIdTable) is
     * a stable hash of the id.
     */
    struct PrototypeDef
    {
        std::string id;         // e.g. "default:cactus"
        std::string displayName;
        std::string blockId;    // linked physical block (must exist in the BlockRegistry)
        // Declared capabilities/slots, e.g. "contact.damage". Pure
        // declarations for now; the behaviour layer arrives with the
        // signal/slot system (M07).
        std::vector<std::string> capabilities;
    };

    /**
     * Generic id-keyed registry with insertion order and strict
     * duplicates. Lookup is O(1); iteration is deterministic.
     */
    template <typename T>
    class Registry
    {
    public:
        const T *find( const std::string &id ) const
        {
            const auto it = mEntries.find( id );
            return it == mEntries.end() ? nullptr : &it->second;
        }

        const T &get( const std::string &id ) const
        {
            const T *entry = find( id );
            if( !entry )
                throw RegistryError( "unknown id '" + id + "'" );
            return *entry;
        }

        bool contains( const std::string &id ) const { return mEntries.count( id ) != 0; }

        std::size_t size() const { return mEntries.size(); }
        bool empty() const { return mEntries.empty(); }

        const std::vector<std::string> &ids() const { return mOrder; }

        /** Inserts or fails with a clear message (no silent override). */
        void insert( const T &entry )
        {
            if( !mEntries.emplace( entry.id, entry ).second )
                throw RegistryError( "duplicate id '" + entry.id + "'" );
            mOrder.push_back( entry.id );
        }

    private:
        std::unordered_map<std::string, T> mEntries;
        std::vector<std::string> mOrder;
    };

    using BlockRegistry = Registry<BlockDef>;
    using BiomeRegistry = Registry<BiomeDef>;
    using ResourceRegistry = Registry<ResourceDef>;
    using PrototypeRegistry = Registry<PrototypeDef>;

} // namespace world
