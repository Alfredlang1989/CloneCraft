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
                const world::omnigrid_i128 current = static_cast<world::omnigrid_i128>( result.remainder ) * radix + digit;
                quotientDigit = static_cast<std::int64_t>( current / divisor );
                result.remainder = static_cast<std::int64_t>( current % divisor );
            };
            step( world::REGIONS_PER_SECTOR_EDGE, axis.region, result.quotient.region );
            step( world::SECTIONS_PER_REGION_EDGE, axis.section, result.quotient.section );
            step( world::GROUPS_PER_SECTION_EDGE, axis.group, result.quotient.group );
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
            std::array<double, 9> c{};
            double phaseXr = 0.0;
            double phaseYr = 0.0;
            double phaseZr = 0.0;
            std::uint64_t groupEpoch = 0;
            bool valid = false;
        };

        Impl( std::uint64_t rootSeed, std::uint64_t perFieldSeed, MacroWarpSettings cfg )
