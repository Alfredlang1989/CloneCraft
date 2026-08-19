#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace world
{
    /**
     * Error reported by registry parsing/validation. The message always
     * mentions the source (file path or "inline"), the entry position and
     * what exactly is wrong, e.g.
     *
     *   MODS/Default/blocks.json: entry 2: missing required field 'displayName'
     *   MODS/Default/blocks.json: entry 3 (id=core:stone): duplicate id
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
        // Generic lexicographic ordering (channel order r,g,b,a). Enables
        // Rgba8 as a std::map key (e.g. renderer tint variants) without any
        // block-name special-casing.
        friend bool operator<( const Rgba8 &l, const Rgba8 &r )
        {
            if( l.r != r.r ) return l.r < r.r;
            if( l.g != r.g ) return l.g < r.g;
            if( l.b != r.b ) return l.b < r.b;
            return l.a < r.a;
        }
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
        // Optional registered uint32 property whose packed value is
        // interpreted as 0xRRGGBBAA by the generic render projection.
        // Zero means no per-instance tint override. The property id and all
        // values remain content data; the renderer knows no gameplay states.
        std::string visualTintProperty;

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
     * Runtime value of a data-driven sidecar/property type (M05). Mirrors
     * SidecarDef::defaultValue: integer types store std::uint32_t, the Float
     * value type stores float. The alternative a value must hold is enforced
     * by the resolver (WorldState) against the SidecarDef::valueType, so a
     * sidecar never mixes alternatives.
     */
    using PropertyValue = std::variant<std::uint32_t, float>;

    /**
     * A logical property an object prototype supports (M05). The id refers
     * to a data-driven sidecar type (sidecars.json); defaultValue is the
     * prototype-specific value the object's blocks expose when no explicit
     * stored state exists. This is what the unified world state resolves
     * instead of a global sidecar default: a property exists for an object
     * only when its prototype declares it (issue #3 section 5).
     */
    struct PrototypePropertyDef
    {
        std::string id;                 // sidecar property id, e.g. "core:orientation"
        PropertyValue defaultValue = 0u; // object-type default for that property
    };

    /**
     * Registered gameplay object/block prototype (e.g. "default:cactus").
     *
     * Prototypes are the *logical* identity layer on top of physical block
     * ids: a voxel stores a compact block index, while gameplay refers to a
     * stable namespaced prototype id. The block-to-prototype mapping is
     * strictly 1:1: each blockId is claimed by at most one prototype, so
     * logical identity never depends on registry order. (If sharing a block
     * between prototypes ever becomes necessary, identity must move into
     * per-block state, e.g. sidecars/ECS (M04/M05) - not into this bridge.)
     *
     * `properties` is the prototype-aware declaration of which sidecar
     * properties this object type supports and their defaults (M05). It is
     * what answers "does this object have property X" and what the unified
     * world state resolves; sidecar state is the per-block *override* of
     * that default. A prototype may later also own event/action hooks (M07).
     * Ids are always namespaced (<namespace>:<name>, both non-empty) and
     * never depend on load order. The runtime handle (PrototypeIdTable)
     * is a stable hash of the id.
     */
    struct PrototypeDef
    {
        std::string id;         // e.g. "default:cactus"
        std::string displayName;
        std::string blockId;    // linked physical block (must exist in the BlockRegistry,
                                // claimed by at most one prototype)
        // Declared capabilities/slots, e.g. "contact.damage". Pure
        // declarations for now; the behaviour layer arrives with the
        // signal/slot system (M07).
        std::vector<std::string> capabilities;
        // Supported logical properties + their prototype defaults (M05).
        std::vector<PrototypePropertyDef> properties;
    };

    /** Logical value schema of a data-driven sidecar type. */
    enum class SidecarValueType : std::uint8_t
    {
        Uint8,
        Uint16,
        Uint32,
        Float
    };

    /**
     * Target scope of a registered sidecar/property type (M01-B, issue #20).
     * The value is the logical address tier the property may be stored at;
     * the canonical hierarchy is Sector -> Region -> Section -> ChunkGroup ->
     * Chunk -> Block. Section/Region/Sector are logical address tiers and are
     * never materialized as containers by the sidecar layer.
     */
    enum class SidecarScope : std::uint8_t
    {
        Block,
        Chunk,
        ChunkGroup,
        Section,
        Region,
        Sector
    };

    /** Storage strategy of a sidecar type (issue #3, section 5.1). */
    enum class SidecarStorageStrategy : std::uint8_t
    {
        Sparse,  // per-entry allocation, default is implicit
        Dense    // one slot per block position (future)
    };

    /** Data-driven id of the orientation pilot type (MODS/Default/sidecars.json). */
    inline constexpr const char *CORE_ORIENTATION_SIDECAR = "core:orientation";

    /**
     * Registered sidecar type (issue #3, section 5.1). Mods declare new
     * sidecar types in sidecars.json; the optimized C++ storage per sidecar
     * may differ from this metadata. The serialization version prepares the
     * persistence layer (M09) without implementing it yet.
     *
     * The runtime pilot (M04) is hardwired to the orientation sidecar; the
     * registry-driven resolver that stores values for *any* declared type
     * arrives with M05. No per-field unique_ptr members are added to Chunk.
     *
     * `scope` (M01-B, #20) is the mandatory target tier. Data files must
     * declare it explicitly - the loader rejects sidecar types without a
     * scope key and any unknown scope string (RegistryError). The C++ member
     * default Block exists only for programmatic SidecarDef{} fixtures and
     * is never used to guess a data file's scope. core:orientation is
     * explicitly block-scoped.
     */
    struct SidecarDef
    {
        std::string id;                    // stable namespaced id, e.g. "core:orientation"
        std::string displayName;
        SidecarValueType valueType = SidecarValueType::Uint8;
        SidecarScope scope = SidecarScope::Block; // M01-B target tier
        // Typed default: integer default for uint8/16/32, float default for
        // the Float value type. Validated per valueType when parsing.
        std::variant<std::uint32_t, float> defaultValue = 0u;
        std::uint32_t bitWidth = 0;        // compact-encoding hint (0 = full type width);
                                           // metadata for storage/serialization (M09) only
        SidecarStorageStrategy storage = SidecarStorageStrategy::Sparse;
        bool persist = true;               // persistence policy
        std::uint32_t serializationVersion = 1;
    };
    /**
     * Whether a PropertyValue fits a sidecar type's declared valueType and
     * compact bitWidth. Shared single source of truth used both at load time
     * (prototype properties are validated against sidecars.json, ADR-027) and
     * at runtime (WorldState::set gate), so a value that the registry accepts
     * can never be rejected by the resolver and vice versa.
     */
    inline bool valueFitsSidecarDef( const SidecarDef &def, const PropertyValue &value )
    {
        if( def.valueType == SidecarValueType::Float )
        {
            // Round 6: a valid sidecar float is FINITE. NaN/Infinity would
            // poison the equality-based sparse semantics (NaN != NaN: every
            // write would look like a change, the default-removal threshold
            // could never match) and cannot be represented in regular JSON
            // anyway.
            return std::holds_alternative<float>( value ) &&
                   std::isfinite( std::get<float>( value ) );
        }
        if( !std::holds_alternative<std::uint32_t>( value ) )
            return false;
        const std::uint64_t v = std::get<std::uint32_t>( value );
        switch( def.valueType )
        {
            case SidecarValueType::Uint8:
                if( v > 0xFFu ) return false;
                break;
            case SidecarValueType::Uint16:
                if( v > 0xFFFFu ) return false;
                break;
            case SidecarValueType::Uint32:
                break;
            case SidecarValueType::Float:
                return false; // handled above
            default:
                return false; // unknown enum value: never fits (round 5)
        }
        if( def.bitWidth != 0u )
        {
            if( def.bitWidth >= 32u )
                return true; // full uint32 range
            if( v >= ( static_cast<std::uint64_t>( 1u ) << def.bitWidth ) )
                return false; // value does not fit the declared compact width
        }
        return true;
    }

    /**
     * Content ids are stable contracts and must be <namespace>:<name>.
     * Single definition shared by loader and runtime (M01-B review round 4).
     */
    inline bool isNamespacedId( const std::string &id )
    {
        const std::size_t separator = id.find( ':' );
        if( separator == std::string::npos )
            return false;
        if( separator == 0u || separator + 1u >= id.size() )
            return false;
        return id.find( ':', separator + 1u ) == std::string::npos;
    }

    /**
     * Structural invariants of a registered sidecar type (M01-B review round
     * 4/5): ONE shared validation source used by the JSON loader AND the
     * runtime (programmatic SidecarDef{} constructions). A definition that
     * the loader rejects can therefore never be inserted or stored by the
     * runtime either.
     *
     * Rules:
     *  - enum members are exactly the declared alternatives - a programmatic
     *    static_cast to an out-of-range SidecarScope/SidecarValueType/
     *    SidecarStorageStrategy is invalid (round 5);
     *  - namespaced id;
     *  - bitWidth 0 (full type width) or in 1..32 - the loader and the
     *    runtime share this exact semantic;
     *  - bitWidth only for integer value types (never float);
     *  - defaultValue fits the declared valueType/bitWidth
     *    (valueFitsSidecarDef);
     *  - hierarchy scopes (chunk .. sector) are sparse by contract: dense
     *    storage is rejected above the block tier (block-dense stays older
     *    architecture);
     *  - serializationVersion >= 1.
     */
    inline void validateSidecarDef( const SidecarDef &def, const std::string &where )
    {
        switch( def.scope )
        {
            case SidecarScope::Block:
            case SidecarScope::Chunk:
            case SidecarScope::ChunkGroup:
            case SidecarScope::Section:
            case SidecarScope::Region:
            case SidecarScope::Sector:
                break;
            default:
                throw RegistryError( where + ": sidecar '" + def.id +
                                     "' has an invalid SidecarScope" );
        }
        switch( def.valueType )
        {
            case SidecarValueType::Uint8:
            case SidecarValueType::Uint16:
            case SidecarValueType::Uint32:
            case SidecarValueType::Float:
                break;
            default:
                throw RegistryError( where + ": sidecar '" + def.id +
                                     "' has an invalid SidecarValueType" );
        }
        switch( def.storage )
        {
            case SidecarStorageStrategy::Sparse:
            case SidecarStorageStrategy::Dense:
                break;
            default:
                throw RegistryError( where + ": sidecar '" + def.id +
                                     "' has an invalid SidecarStorageStrategy" );
        }
        if( !isNamespacedId( def.id ) )
            throw RegistryError( where + ": sidecar id '" + def.id +
                                 "' must be namespaced as <namespace>:<name>" );
        if( def.bitWidth > 32u )
            throw RegistryError( where + ": sidecar '" + def.id +
                                 "' bitWidth " + std::to_string( def.bitWidth ) +
                                 " must be in 1..32 (0 = full type width)" );
        if( def.valueType == SidecarValueType::Float && def.bitWidth != 0u )
            throw RegistryError( where + ": sidecar '" + def.id +
                                 "' bitWidth only applies to integer value types" );
        if( !valueFitsSidecarDef( def, def.defaultValue ) )
            throw RegistryError( where + ": sidecar '" + def.id +
                                 "' defaultValue does not fit the declared valueType/bitWidth" );
        if( def.scope != SidecarScope::Block && def.storage == SidecarStorageStrategy::Dense )
            throw RegistryError( where + ": sidecar '" + def.id +
                                 "' 'storage' 'dense' is not supported for sidecar scopes "
                                 "above 'block' (hierarchy sidecars are sparse)" );
        if( def.serializationVersion == 0u )
            throw RegistryError( where + ": sidecar '" + def.id +
                                 "' serializationVersion must be >= 1" );
    }

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

    /**
     * Prototype registry with a persistent block-claim index (blockId ->
     * prototype). The 1:1 contract - each physical block is claimed by at
     * most one prototype, so `prototypeForBlock` never depends on registry
     * order - is enforced across ALL load calls, not only within a single
     * parse invocation. Re-loading the same file into a registry that
     * already contains the claims is still rejected as a duplicate.
     */
    class PrototypeRegistry : public Registry<PrototypeDef>
    {
    public:
        void insert( const PrototypeDef &entry )
        {
            if( const std::string *owner = claimedBy( entry.blockId ) )
                throw RegistryError( "blockId '" + entry.blockId +
                                     "' is already claimed by prototype '" + *owner + "'" );
            Registry<PrototypeDef>::insert( entry );
            mBlockClaims.emplace( entry.blockId, entry.id );
        }

        /** Prototype id claiming the block, or nullptr when unclaimed. */
        const std::string *claimedBy( const std::string &blockId ) const
        {
            const auto it = mBlockClaims.find( blockId );
            return it == mBlockClaims.end() ? nullptr : &it->second;
        }

    private:
        std::unordered_map<std::string, std::string> mBlockClaims;
    };

    using SidecarRegistry = Registry<SidecarDef>;

    /** Runtime gate (M01-B review round 4): every SidecarDef inserted - by
     *  the JSON loader or programmatically - passes the same structural
     *  validation. A definition the loader would reject can never enter the
     *  registry via code paths either, so WorldState can never store a
     *  definition that violates the sparse/dense or shape rules. */
    template <>
    inline void Registry<SidecarDef>::insert( const SidecarDef &entry )
    {
        validateSidecarDef( entry, "sidecar '" + entry.id + "'" );
        if( !mEntries.emplace( entry.id, entry ).second )
            throw RegistryError( "duplicate id '" + entry.id + "'" );
        mOrder.push_back( entry.id );
    }
} // namespace world
