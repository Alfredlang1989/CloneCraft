#pragma once

#include "world/coordinates/Coords.h"

#include <cstdint>
#include <memory>

namespace worldgen
{
    struct MacroWarpSettings
    {
        // Macro cell width in physical ChunkGroups. This is deliberately
        // independent of the Region/Sector radix so phase 2 can enlarge the
        // super-coordinate radices without changing terrain wavelength.
        std::int64_t cellGroups = 32;
        double horizontalAmplitudeBlocks = 64.0;
        bool enabled = true;
    };

    /**
     * v16-compatible OpenSimplex2S sampled from Clonecraft's hierarchical
     * address space.
     *
     * The large address is mapped once per ChunkGroup into OpenSimplex's
     * transformed lattice phase. Individual noise calls then use only the
     * 0..255 group-local block position plus small domain-warp offsets.
     * No flattened world coordinate and no astronomical floating-point value
     * is ever formed.
     *
     * A very-low-frequency, hierarchy-hashed coordinate warp is evaluated at
     * the current sample. It breaks the native finite OpenSimplex permutation
     * repeat without replacing the v16 noise morphology.
     */
    class MappedOpenSimplexNoise
    {
    public:
        MappedOpenSimplexNoise( std::uint64_t worldSeed, std::uint64_t fieldSeed,
                                MacroWarpSettings settings = {} );
        ~MappedOpenSimplexNoise();

        MappedOpenSimplexNoise( MappedOpenSimplexNoise && ) noexcept;
        MappedOpenSimplexNoise &operator=( MappedOpenSimplexNoise && ) noexcept;
        MappedOpenSimplexNoise( const MappedOpenSimplexNoise & ) = delete;
        MappedOpenSimplexNoise &operator=( const MappedOpenSimplexNoise & ) = delete;

        void setSample( const world::BlockAddress &sample ) const;

        /** Fast chunk-sampling path. The ChunkGroup mapping is prepared once for
         * the slice and setSliceSample only changes small group-local digits.
         * This keeps hierarchical address work out of the 4096-voxel hot path. */
        void beginChunkXSlice( const world::ChunkAddress &chunk, std::int64_t localX ) const;
        void setSliceSample( std::int64_t localY, std::int64_t localZ ) const;

        // v16-like scale API. Scales are small local doubles; the mapper treats
        // their exact IEEE-754 value as a binary rational when folding the huge
        // integer address into the OpenSimplex lattice phase.
        double noise2( double scale, std::uint64_t salt,
                       double offsetX = 0.0, double offsetZ = 0.0 ) const;
        double noise3( double scaleX, double scaleY, double scaleZ,
                       std::uint64_t salt, double offsetX = 0.0,
                       double offsetY = 0.0, double offsetZ = 0.0 ) const;

        const MacroWarpSettings &macroWarpSettings() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
} // namespace worldgen
