#include "world/worldgen/NoiseSource.h"

#include "OpenSimplex2/OpenSimplex2S.hpp"

namespace worldgen
{
    struct NoiseSource::Impl
    {
        explicit Impl( std::uint64_t seed ) : noise( seed ) {}
        OpenSimplex2S noise;
    };

    NoiseSource::NoiseSource( std::uint64_t seed )
        : mImpl( std::make_unique<Impl>( seed ) )
    {
    }

    NoiseSource::~NoiseSource() = default;

    double NoiseSource::noise2( double x, double z ) const
    {
        return mImpl->noise.noise2( x, z );
    }

    double NoiseSource::noise3( double x, double y, double z ) const
    {
        // Y is vertical: use XZBeforeY so noise is (x, y, z)-consistent.
        return mImpl->noise.noise3_XZBeforeY( x, y, z );
    }

    double NoiseSource::noise2Lattice( double xs, double ys ) const
    {
        return mImpl->noise.noise2_Lattice( xs, ys );
    }

    double NoiseSource::noise3BccLattice( double xr, double yr, double zr ) const
    {
        return mImpl->noise.noise3_BCC_Lattice( xr, yr, zr );
    }
} // namespace worldgen