#pragma once

#include <cstdint>
#include <memory>

namespace worldgen
{
    /**
     * Deterministic 2D/3D noise source over OpenSimplex2S (project-local
     * third-party code in third_party/OpenSimplex2). Noise is a pure
     * function of (seed, coordinates): the same seed + coordinates always
     * yield the same values. Output range is roughly [-1, 1].
     */
    class NoiseSource
    {
    public:
        explicit NoiseSource( std::uint64_t seed );
        ~NoiseSource();

        NoiseSource( const NoiseSource & ) = delete;
        NoiseSource &operator=( const NoiseSource & ) = delete;

        /** 2D noise; x, z are world coordinates (noise units ~1). */
        double noise2( double x, double z ) const;

        /** 3D noise; y is the vertical axis. */
        double noise3( double x, double y, double z ) const;

        /** Direct evaluation in OpenSimplex2S transformed lattice space. */
        double noise2Lattice( double xs, double ys ) const;
        double noise3BccLattice( double xr, double yr, double zr ) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
} // namespace worldgen