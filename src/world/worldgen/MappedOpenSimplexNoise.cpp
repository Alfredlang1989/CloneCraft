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
            // The mapped group phase is already in [0,2048). Local chunk/group
            // contributions are tiny, so normal worldgen almost always needs at
            // most one wrap. Keep the expensive fmod path only for unusually
            // large script-supplied domain offsets.
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

        std::uint64_t hashAxis( std::uint64_t h, const Axis &a,
                                std::uint64_t tag ) noexcept
        {
            h = mix64( h ^ tag );
            h = mix64( h ^ std::bit_cast<std::uint64_t>( a.sector ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.region ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.group ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.chunk ) );
            h = mix64( h ^ static_cast<std::uint64_t>( a.block ) );
            return h;
        }

        double hashUnitSigned( std::uint64_t h ) noexcept
        {
            const double unit = static_cast<double>( mix64( h ) >> 11u ) *
                                ( 1.0 / 9007199254740992.0 );
            return unit * 2.0 - 1.0;
        }

        double fade( double t ) noexcept
        {
            return t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 );
        }
        double lerp( double a, double b, double t ) noexcept
        {
            return a + ( b - a ) * t;
        }

        struct BinaryDouble
        {
            bool negative = false;
            std::uint64_t mantissa = 0;
            int exponent2 = 0;
        };

        BinaryDouble decomposeDouble( double value )
        {
            if( !std::isfinite( value ) )
                throw std::invalid_argument( "noise mapping coefficient must be finite" );
            if( value == 0.0 ) return {};

            const std::uint64_t bits = std::bit_cast<std::uint64_t>( value );
            const bool negative = ( bits >> 63u ) != 0u;
            const std::uint64_t exponentBits = ( bits >> 52u ) & 0x7FFu;
            const std::uint64_t fraction = bits & ( ( UINT64_C( 1 ) << 52u ) - 1u );
            if( exponentBits == 0u )
                return { negative, fraction, 1 - 1023 - 52 };
            return { negative, ( UINT64_C( 1 ) << 52u ) | fraction,
                     static_cast<int>( exponentBits ) - 1023 - 52 };
        }

        U128 maskBits( int bits )
        {
            if( bits <= 0 || bits >= 128 )
                throw std::invalid_argument( "binary phase width outside uint128 support" );
            return ( static_cast<U128>( 1u ) << bits ) - 1u;
        }

        U128 signedModPow2( std::int64_t value, int bits )
        {
            const U128 mask = maskBits( bits );
            if( value >= 0 ) return static_cast<U128>( value ) & mask;
            const std::uint64_t magnitude =
                static_cast<std::uint64_t>( -( value + 1 ) ) + UINT64_C( 1 );
            return ( static_cast<U128>( 0u ) - static_cast<U128>( magnitude ) ) & mask;
        }

        U128 foldDigit( U128 current, std::uint64_t radix, std::uint64_t digit,
                        U128 mask ) noexcept
        {
            // Unsigned __int128 multiplication is defined modulo 2^128. We only
            // retain lower bits, so overflow above bit 127 cannot corrupt them.
            return ( current * static_cast<U128>( radix ) + static_cast<U128>( digit ) ) & mask;
        }

        U128 axisModuloPow2( const Axis &axis, int bits )
        {
            const U128 mask = maskBits( bits );
            U128 value = signedModPow2( axis.sector, bits );
            value = foldDigit( value, static_cast<std::uint64_t>( world::REGIONS_PER_SECTOR_EDGE ),
                               static_cast<std::uint64_t>( axis.region ), mask );
            value = foldDigit( value, static_cast<std::uint64_t>( world::GROUPS_PER_REGION_EDGE ),
                               static_cast<std::uint64_t>( axis.group ), mask );
            value = foldDigit( value, static_cast<std::uint64_t>( world::CHUNKS_PER_GROUP_EDGE ),
                               static_cast<std::uint64_t>( axis.chunk ), mask );
            value = foldDigit( value, static_cast<std::uint64_t>( world::BLOCKS_PER_CHUNK_EDGE ),
                               static_cast<std::uint64_t>( axis.block ), mask );
            return value;
        }

        long double u128ToLongDouble( U128 value ) noexcept
        {
            const std::uint64_t lo = static_cast<std::uint64_t>( value );
            const std::uint64_t hi = static_cast<std::uint64_t>( value >> 64u );
            return std::ldexp( static_cast<long double>( hi ), 64 ) +
                   static_cast<long double>( lo );
        }

        /** Exact phase of coefficient * hierarchical integer axis modulo 2048.
         * The coefficient is treated as its exact IEEE-754 binary rational. */
        double coefficientPhase( const Axis &axis, double coefficient )
        {
            const BinaryDouble binary = decomposeDouble( coefficient );
            if( binary.mantissa == 0u ) return 0.0;

            const int fractionalBits = binary.exponent2 < 0 ? -binary.exponent2 : 0;
            const int phaseBits = LATTICE_PERIOD_BITS + fractionalBits;
            if( phaseBits >= 128 )
                throw std::invalid_argument(
                    "noise scale is too tiny for the exact uint128 phase mapper" );
            const U128 mask = maskBits( phaseBits );
            U128 factor = static_cast<U128>( binary.mantissa );
            if( binary.exponent2 > 0 )
            {
                if( binary.exponent2 >= phaseBits ) factor = 0u;
                else factor = ( factor << binary.exponent2 ) & mask;
            }

            U128 product = ( axisModuloPow2( axis, phaseBits ) * factor ) & mask;
            if( binary.negative ) product = ( static_cast<U128>( 0u ) - product ) & mask;
            const long double scaled = std::ldexp( u128ToLongDouble( product ), -fractionalBits );
            return wrapLattice( static_cast<double>( scaled ) );
        }

        double sumPhase( double a, double b ) noexcept
        {
            return wrapLattice( a + b );
        }
        double sumPhase( double a, double b, double c ) noexcept
        {
            return wrapLattice( a + b + c );
        }

        struct GroupAxisDivision
        {
            Axis quotient{};
            std::int64_t remainder = 0;
        };

        GroupAxisDivision divideGroupAxis( Axis axis, std::int64_t divisor )
        {
            if( divisor <= 0 ) throw std::invalid_argument( "macro cell divisor must be positive" );
            axis.chunk = 0;
            axis.block = 0;
            GroupAxisDivision result{};
            result.quotient.sector = world::floorDiv( axis.sector, divisor );
            result.remainder = world::floorMod( axis.sector, divisor );

            auto step = [&]( std::int64_t radix, std::int64_t digit,
                             std::int64_t &quotientDigit ) {
                const world::clonecraft_i128 current = static_cast<world::clonecraft_i128>( result.remainder ) * radix + digit;
                quotientDigit = static_cast<std::int64_t>( current / divisor );
                result.remainder = static_cast<std::int64_t>( current % divisor );
            };
            step( world::REGIONS_PER_SECTOR_EDGE, axis.region, result.quotient.region );
            step( world::GROUPS_PER_REGION_EDGE, axis.group, result.quotient.group );
            result.quotient.chunk = 0;
            result.quotient.block = 0;
            return result;
        }

        Axis plusMacroCell( const Axis &base, std::int64_t delta )
        {
            Axis out{};
            if( !world::detail::tryOffsetGroupAxis( base, delta, out ) )
                throw std::overflow_error( "macro warp cell crossed SectorCoord range" );
            return out;
        }

        struct MacroFrame2
        {
            std::int64_t remainderX = 0;
            std::int64_t remainderZ = 0;
            std::array<double, 4> warpX{};
            std::array<double, 4> warpZ{};
        };

        MacroFrame2 buildMacroFrame2( std::uint64_t worldSeed, const Axis &groupX,
                                      const Axis &groupZ, std::int64_t cellGroups )
        {
            const GroupAxisDivision dx = divideGroupAxis( groupX, cellGroups );
            const GroupAxisDivision dz = divideGroupAxis( groupZ, cellGroups );
            const Axis x1 = plusMacroCell( dx.quotient, 1 );
            const Axis z1 = plusMacroCell( dz.quotient, 1 );
            const std::array<Axis, 2> xs{ dx.quotient, x1 };
            const std::array<Axis, 2> zs{ dz.quotient, z1 };

            MacroFrame2 frame{};
            frame.remainderX = dx.remainder;
            frame.remainderZ = dz.remainder;
            for( int ix = 0; ix < 2; ++ix )
                for( int iz = 0; iz < 2; ++iz )
                {
                    std::uint64_t h = mix64( worldSeed ^ 0x4D4143524F325741ULL );
                    h = hashAxis( h, xs[static_cast<std::size_t>( ix )],
                                  0xA24BAED4963EE407ULL );
                    h = hashAxis( h, zs[static_cast<std::size_t>( iz )],
                                  0x9FB21C651E98DF25ULL );
                    const std::size_t slot = static_cast<std::size_t>( ix ) * 2u +
                                             static_cast<std::size_t>( iz );
                    frame.warpX[slot] = hashUnitSigned( h ^ 0x58574152504C4F57ULL );
                    frame.warpZ[slot] = hashUnitSigned( h ^ 0x5A574152504C4F57ULL );
                }
            return frame;
        }

        std::pair<double, double> evaluateMacroFrame2( const MacroFrame2 &frame,
                                                        std::int64_t localGroupX,
                                                        std::int64_t localGroupZ,
                                                        const MacroWarpSettings &settings )
        {
            if( !settings.enabled || settings.horizontalAmplitudeBlocks == 0.0 )
                return { 0.0, 0.0 };
            const double groupFractionX = static_cast<double>( localGroupX ) /
                                          static_cast<double>( world::BLOCKS_PER_GROUP_EDGE );
            const double groupFractionZ = static_cast<double>( localGroupZ ) /
                                          static_cast<double>( world::BLOCKS_PER_GROUP_EDGE );
            const double tx = fade( ( static_cast<double>( frame.remainderX ) + groupFractionX ) /
                                    static_cast<double>( settings.cellGroups ) );
            const double tz = fade( ( static_cast<double>( frame.remainderZ ) + groupFractionZ ) /
                                    static_cast<double>( settings.cellGroups ) );
            const auto interp = [&]( const std::array<double, 4> &v ) {
                return lerp( lerp( v[0], v[2], tx ), lerp( v[1], v[3], tx ), tz );
            };
            return { interp( frame.warpX ) * settings.horizontalAmplitudeBlocks,
                     interp( frame.warpZ ) * settings.horizontalAmplitudeBlocks };
        }

    } // namespace

    struct MappedOpenSimplexNoise::Impl
    {
        struct NoiseEntry
        {
            std::uint64_t salt = 0;
            std::unique_ptr<NoiseSource> noise;
        };
        struct Call2
        {
            std::uint64_t salt = 0;
            double scale = 0.0;
            double cxx = 0.0;
            double cxz = 0.0;
            double phaseXs = 0.0;
            double phaseYs = 0.0;
            std::uint64_t groupEpoch = 0;
            bool valid = false;
        };
        struct Call3
        {
            std::uint64_t salt = 0;
            double sx = 0.0, sy = 0.0, sz = 0.0;
            std::array<double, 9> c{}; // rows xr,yr,zr; columns X,Y,Z
            double phaseXr = 0.0;
            double phaseYr = 0.0;
            double phaseZr = 0.0;
            std::uint64_t groupEpoch = 0;
            bool valid = false;
        };

        Impl( std::uint64_t rootSeed, std::uint64_t perFieldSeed, MacroWarpSettings cfg )
            : worldSeed( rootSeed ), fieldSeed( perFieldSeed ), settings( cfg )
        {
            if( settings.cellGroups <= 0 )
                throw std::invalid_argument( "macro warp cellGroups must be positive" );
            if( !std::isfinite( settings.horizontalAmplitudeBlocks ) ||
                settings.horizontalAmplitudeBlocks < 0.0 )
                throw std::invalid_argument( "macro warp amplitude must be finite and non-negative" );
        }

        std::uint64_t worldSeed = 0;
        std::uint64_t fieldSeed = 0;
        MacroWarpSettings settings{};
        bool sampleValid = false;
        world::GroupAddress group{};
        std::int64_t localGroupX = 0, localGroupY = 0, localGroupZ = 0;
        double macroWarpX = 0.0, macroWarpZ = 0.0;
        Axis groupOriginX{}, groupOriginY{}, groupOriginZ{};
        MacroFrame2 macro{};
        bool slicePrepared = false;
        std::int64_t sliceLocalX = 0;
        std::int64_t sliceBaseY = 0;
        std::int64_t sliceBaseZ = 0;
        std::array<double, static_cast<std::size_t>( world::BLOCKS_PER_CHUNK_EDGE )> sliceWarpX{};
        std::array<double, static_cast<std::size_t>( world::BLOCKS_PER_CHUNK_EDGE )> sliceWarpZ{};
        std::array<NoiseEntry, CACHE_SIZE> noises{};
        std::array<Call2, CACHE_SIZE> calls2{};
        std::array<Call3, CACHE_SIZE> calls3{};
        std::uint64_t groupEpoch = 0;

        void selectGroup( const world::GroupAddress &selected )
        {
            world::requireCanonical( selected );
            if( sampleValid && group == selected ) return;
            group = selected;
            const world::BlockAddress origin = world::chunkOrigin( { group, {} } );
            groupOriginX = world::blockAxisX( origin );
            groupOriginY = world::blockAxisY( origin );
            groupOriginZ = world::blockAxisZ( origin );
            macro = buildMacroFrame2( worldSeed, groupOriginX, groupOriginZ,
                                      settings.cellGroups );
            ++groupEpoch;
            if( groupEpoch == 0u )
            {
                // Practically unreachable, but keep epoch wrap deterministic.
                groupEpoch = 1u;
                for( Call2 &call : calls2 ) call.groupEpoch = 0u;
                for( Call3 &call : calls3 ) call.groupEpoch = 0u;
            }
            slicePrepared = false;
            sampleValid = true;
        }

        void setLocalSample( std::int64_t x, std::int64_t y, std::int64_t z )
        {
            localGroupX = x;
            localGroupY = y;
            localGroupZ = z;
            const auto warp = evaluateMacroFrame2( macro, x, z, settings );
            macroWarpX = warp.first;
            macroWarpZ = warp.second;
            sampleValid = true;
        }

        void setSample( const world::BlockAddress &sample )
        {
            world::requireCanonical( sample );
            selectGroup( sample.chunk.group );
            const world::LocalGroupBlockCoord local = world::localBlockInGroup( sample );
            setLocalSample( local.x, local.y, local.z );
        }

        void beginChunkXSlice( const world::ChunkAddress &chunk, std::int64_t lx )
        {
            world::requireCanonical( chunk );
            if( lx < 0 || lx >= world::BLOCKS_PER_CHUNK_EDGE )
                throw std::out_of_range( "mapped noise X slice outside chunk" );
            selectGroup( chunk.group );
            sliceLocalX = chunk.chunk.x * world::BLOCKS_PER_CHUNK_EDGE + lx;
            sliceBaseY = chunk.chunk.y * world::BLOCKS_PER_CHUNK_EDGE;
            sliceBaseZ = chunk.chunk.z * world::BLOCKS_PER_CHUNK_EDGE;
            for( std::int64_t lz = 0; lz < world::BLOCKS_PER_CHUNK_EDGE; ++lz )
            {
                const auto warp = evaluateMacroFrame2( macro, sliceLocalX, sliceBaseZ + lz, settings );
                sliceWarpX[static_cast<std::size_t>( lz )] = warp.first;
                sliceWarpZ[static_cast<std::size_t>( lz )] = warp.second;
            }
            slicePrepared = true;
            sampleValid = true;
        }

        void setSliceSample( std::int64_t ly, std::int64_t lz )
        {
            if( !slicePrepared ) throw std::logic_error( "mapped noise chunk slice was not prepared" );
            if( ly < 0 || ly >= world::BLOCKS_PER_CHUNK_EDGE ||
                lz < 0 || lz >= world::BLOCKS_PER_CHUNK_EDGE )
                throw std::out_of_range( "mapped noise local sample outside chunk slice" );
            localGroupX = sliceLocalX;
            localGroupY = sliceBaseY + ly;
            localGroupZ = sliceBaseZ + lz;
            macroWarpX = sliceWarpX[static_cast<std::size_t>( lz )];
            macroWarpZ = sliceWarpZ[static_cast<std::size_t>( lz )];
            sampleValid = true;
        }

        NoiseSource &noiseFor( std::uint64_t salt )
        {
            NoiseEntry &entry = noises[static_cast<std::size_t>( salt ) & ( CACHE_SIZE - 1u )];
            if( !entry.noise || entry.salt != salt )
            {
                entry.salt = salt;
                entry.noise = std::make_unique<NoiseSource>( deriveNoiseSeed( fieldSeed, salt ) );
            }
            return *entry.noise;
        }

        Call2 &call2( double scale, std::uint64_t salt )
        {
            Call2 &call = calls2[static_cast<std::size_t>( salt ) & ( CACHE_SIZE - 1u )];
            if( !call.valid || call.salt != salt || call.scale != scale )
            {
                validateScale( scale );
                call.salt = salt;
                call.scale = scale;
                call.cxx = scale * ( 1.0 + F2 );
                call.cxz = scale * F2;
                call.valid = true;
                call.groupEpoch = 0u;
            }
            if( call.groupEpoch != groupEpoch )
            {
                call.phaseXs = sumPhase( coefficientPhase( groupOriginX, call.cxx ),
                                         coefficientPhase( groupOriginZ, call.cxz ) );
                call.phaseYs = sumPhase( coefficientPhase( groupOriginX, call.cxz ),
                                         coefficientPhase( groupOriginZ, call.cxx ) );
                call.groupEpoch = groupEpoch;
            }
            return call;
        }

        Call3 &call3( double sx, double sy, double sz, std::uint64_t salt )
        {
            Call3 &call = calls3[static_cast<std::size_t>( salt ) & ( CACHE_SIZE - 1u )];
            if( !call.valid || call.salt != salt || call.sx != sx || call.sy != sy || call.sz != sz )
            {
                validateScale( sx ); validateScale( sy ); validateScale( sz );
                call.salt = salt;
                call.sx = sx; call.sy = sy; call.sz = sz;
                // Exact linear transform used by noise3_XZBeforeY.
                call.c = {
                    sx * ( 1.0 + ROTATE_S2 ), sy * -ROTATE_Y, sz * ROTATE_S2,
                    sx * ROTATE_Y,           sy * ROTATE_Y,  sz * ROTATE_Y,
                    sx * ROTATE_S2,           sy * -ROTATE_Y, sz * ( 1.0 + ROTATE_S2 )
                };
                call.valid = true;
                call.groupEpoch = 0u;
            }
            if( call.groupEpoch != groupEpoch )
            {
                call.phaseXr = sumPhase( coefficientPhase( groupOriginX, call.c[0] ),
                                         coefficientPhase( groupOriginY, call.c[1] ),
                                         coefficientPhase( groupOriginZ, call.c[2] ) );
                call.phaseYr = sumPhase( coefficientPhase( groupOriginX, call.c[3] ),
                                         coefficientPhase( groupOriginY, call.c[4] ),
                                         coefficientPhase( groupOriginZ, call.c[5] ) );
                call.phaseZr = sumPhase( coefficientPhase( groupOriginX, call.c[6] ),
                                         coefficientPhase( groupOriginY, call.c[7] ),
                                         coefficientPhase( groupOriginZ, call.c[8] ) );
                call.groupEpoch = groupEpoch;
            }
            return call;
        }

        double noise2( double scale, std::uint64_t salt,
                       double ox, double oz )
        {
            if( !sampleValid ) throw std::logic_error( "mapped noise sample was not selected" );
            if( !std::isfinite( ox ) || !std::isfinite( oz ) )
                throw std::invalid_argument( "mapped noise offset must be finite" );
            Call2 &f = call2( scale, salt );
            const double x = static_cast<double>( localGroupX ) + macroWarpX + ox;
            const double z = static_cast<double>( localGroupZ ) + macroWarpZ + oz;
            const double xs = wrapLattice( f.phaseXs + f.cxx * x + f.cxz * z );
            const double ys = wrapLattice( f.phaseYs + f.cxz * x + f.cxx * z );
            return noiseFor( salt ).noise2Lattice( xs, ys );
        }

        double noise3( double sx, double sy, double sz,
                       std::uint64_t salt, double ox, double oy, double oz )
        {
            if( !sampleValid ) throw std::logic_error( "mapped noise sample was not selected" );
            if( !std::isfinite( ox ) || !std::isfinite( oy ) || !std::isfinite( oz ) )
                throw std::invalid_argument( "mapped noise offset must be finite" );
            Call3 &f = call3( sx, sy, sz, salt );
            const double x = static_cast<double>( localGroupX ) + macroWarpX + ox;
            const double y = static_cast<double>( localGroupY ) + oy;
            const double z = static_cast<double>( localGroupZ ) + macroWarpZ + oz;
            const double xr = wrapLattice( f.phaseXr + f.c[0]*x + f.c[1]*y + f.c[2]*z );
            const double yr = wrapLattice( f.phaseYr + f.c[3]*x + f.c[4]*y + f.c[5]*z );
            const double zr = wrapLattice( f.phaseZr + f.c[6]*x + f.c[7]*y + f.c[8]*z );
            return noiseFor( salt ).noise3BccLattice( xr, yr, zr );
        }
    };

    MappedOpenSimplexNoise::MappedOpenSimplexNoise( std::uint64_t worldSeed,
                                                    std::uint64_t fieldSeed,
                                                    MacroWarpSettings settings )
        : mImpl( std::make_unique<Impl>( worldSeed, fieldSeed, settings ) )
    {
    }
    MappedOpenSimplexNoise::~MappedOpenSimplexNoise() = default;
    MappedOpenSimplexNoise::MappedOpenSimplexNoise( MappedOpenSimplexNoise && ) noexcept = default;
    MappedOpenSimplexNoise &MappedOpenSimplexNoise::operator=( MappedOpenSimplexNoise && ) noexcept = default;

    void MappedOpenSimplexNoise::setSample( const world::BlockAddress &sample ) const
    {
        mImpl->setSample( sample );
    }
    void MappedOpenSimplexNoise::beginChunkXSlice( const world::ChunkAddress &chunk,
                                                   std::int64_t localX ) const
    {
        mImpl->beginChunkXSlice( chunk, localX );
    }
    void MappedOpenSimplexNoise::setSliceSample( std::int64_t localY,
                                                 std::int64_t localZ ) const
    {
        mImpl->setSliceSample( localY, localZ );
    }
    double MappedOpenSimplexNoise::noise2( double scale, std::uint64_t salt,
                                           double ox, double oz ) const
    {
        return mImpl->noise2( scale, salt, ox, oz );
    }
    double MappedOpenSimplexNoise::noise3( double sx, double sy, double sz,
                                           std::uint64_t salt, double ox,
                                           double oy, double oz ) const
    {
        return mImpl->noise3( sx, sy, sz, salt, ox, oy, oz );
    }
    const MacroWarpSettings &MappedOpenSimplexNoise::macroWarpSettings() const
    {
        return mImpl->settings;
    }
} // namespace worldgen
