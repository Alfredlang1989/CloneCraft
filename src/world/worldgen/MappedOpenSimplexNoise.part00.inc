#include "world/worldgen/MappedOpenSimplexNoise.h"

#include "world/worldgen/NoiseSeed.h"
#include "world/worldgen/NoiseSource.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace worldgen
{
    namespace
    {
        using Axis = world::detail::AxisAddress;
        __extension__ typedef unsigned __int128 U128;

        constexpr double LATTICE_PERIOD = 2048.0;
        constexpr int LATTICE_PERIOD_BITS = 11;
        constexpr double F2 = 0.366025403784439;
        constexpr double ROTATE_S2 = -0.211324865405187;
        constexpr double ROTATE_Y = 0.577350269189626;
        constexpr std::size_t CACHE_SIZE = 64u;

        void validateScale( double scale )
        {
            if( !std::isfinite( scale ) || scale <= 0.0 || scale > 1.0 )
                throw std::invalid_argument(
                    "mapped OpenSimplex scale must be finite and satisfy 0 < scale <= 1" );
        }

        double wrapLattice( double value ) noexcept
        {
            if( value >= 0.0 && value < LATTICE_PERIOD ) return value;
            if( value >= LATTICE_PERIOD && value < LATTICE_PERIOD * 2.0 )
                return value - LATTICE_PERIOD;
            if( value < 0.0 && value >= -LATTICE_PERIOD )
                return value + LATTICE_PERIOD;
            value = std::fmod( value, LATTICE_PERIOD );
            if( value < 0.0 ) value += LATTICE_PERIOD;
            if( value >= LATTICE_PERIOD ) value = 0.0;
            return value;
        }

        std::uint64_t mix64( std::uint64_t x ) noexcept
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = ( x ^ ( x >> 30u ) ) * 0xBF58476D1CE4E5B9ULL;
            x = ( x ^ ( x >> 27u ) ) * 0x94D049BB133111EBULL;
            return x ^ ( x >> 31u );
        }

        bool tryPhase1HashAxis( const Axis &a, Axis &legacy ) noexcept
        {
            auto mulAddI64 = []( std::int64_t value, std::int64_t radix,
                                 std::int64_t digit, std::int64_t &out ) noexcept {
                const world::clonecraft_i128 wide =
                    static_cast<world::clonecraft_i128>( value ) * radix + digit;
                if( wide < std::numeric_limits<std::int64_t>::min() ||
                    wide > std::numeric_limits<std::int64_t>::max() )
                    return false;
                out = static_cast<std::int64_t>( wide );
                return true;
            };
            std::int64_t regionIndex = 0;
            std::int64_t absoluteSection = 0;
            if( !mulAddI64( a.sector, world::REGIONS_PER_SECTOR_EDGE, a.region,
                            regionIndex ) ||
                !mulAddI64( regionIndex, world::SECTIONS_PER_REGION_EDGE, a.section,
                            absoluteSection ) )
                return false;
            constexpr std::int64_t kPhase1GroupsPerRegion = 16;
            legacy = {};
            legacy.sector = absoluteSection;
            legacy.region = a.group / kPhase1GroupsPerRegion;
            legacy.group = a.group % kPhase1GroupsPerRegion;
            legacy.chunk = a.chunk;
            legacy.block = a.block;
            return true;
        }

        std::uint64_t hashAxis( std::uint64_t h, const Axis &a,
                                std::uint64_t tag ) noexcept
        {
            Axis legacy{};
            if( tryPhase1HashAxis( a, legacy ) )
            {
                h = mix64( h ^ tag );
                h = mix64( h ^ std::bit_cast<std::uint64_t>( legacy.sector ) );
                h = mix64( h ^ static_cast<std::uint64_t>( legacy.region ) );
                h = mix64( h ^ static_cast<std::uint64_t>( legacy.group ) );
                h = mix64( h ^ static_cast<std::uint64_t>( legacy.chunk ) );
                h = mix64( h ^ static_cast<std::uint64_t>( legacy.block ) );
                return h;
            }
            h = mix64( h ^ tag );
            h = mix64( h ^ std::bit_cast<std::uint64_t>( a.sector ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.region ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.section ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.group ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.chunk ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.block ) );
            return h;
        }

        double hashUnitSigned( std::uint64_t h ) noexcept
        {
