            const double unit = static_cast<double>( mix64( h ) >> 11u ) *
                                ( 1.0 / 9007199254740992.0 );
            return unit * 2.0 - 1.0;
        }

        double fade( double t ) noexcept { return t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 ); }
        double lerp( double a, double b, double t ) noexcept { return a + ( b - a ) * t; }

        struct BinaryDouble { bool negative=false; std::uint64_t mantissa=0; int exponent2=0; };
        BinaryDouble decomposeDouble( double value )
        {
            if( !std::isfinite( value ) ) throw std::invalid_argument( "noise mapping coefficient must be finite" );
            if( value == 0.0 ) return {};
            const std::uint64_t bits=std::bit_cast<std::uint64_t>(value);
            const bool negative=(bits>>63u)!=0u;
            const std::uint64_t exponentBits=(bits>>52u)&0x7FFu;
            const std::uint64_t fraction=bits&((UINT64_C(1)<<52u)-1u);
            if(exponentBits==0u)return{negative,fraction,1-1023-52};
            return{negative,(UINT64_C(1)<<52u)|fraction,static_cast<int>(exponentBits)-1023-52};
        }
        U128 maskBits(int bits){if(bits<=0||bits>=128)throw std::invalid_argument("binary phase width outside uint128 support");return(static_cast<U128>(1u)<<bits)-1u;}
        U128 signedModPow2(std::int64_t value,int bits){const U128 mask=maskBits(bits);if(value>=0)return static_cast<U128>(value)&mask;const std::uint64_t magnitude=static_cast<std::uint64_t>(-(value+1))+UINT64_C(1);return(static_cast<U128>(0u)-static_cast<U128>(magnitude))&mask;}
        U128 foldDigit(U128 current,std::uint64_t radix,std::uint64_t digit,U128 mask)noexcept{return(current*static_cast<U128>(radix)+static_cast<U128>(digit))&mask;}
        U128 axisModuloPow2(const Axis&axis,int bits)
        {
            const U128 mask=maskBits(bits);U128 value=signedModPow2(axis.sector,bits);
            value=foldDigit(value,static_cast<std::uint64_t>(world::REGIONS_PER_SECTOR_EDGE),static_cast<std::uint64_t>(axis.region),mask);
            value=foldDigit(value,static_cast<std::uint64_t>(world::SECTIONS_PER_REGION_EDGE),static_cast<std::uint64_t>(axis.section),mask);
            value=foldDigit(value,static_cast<std::uint64_t>(world::GROUPS_PER_SECTION_EDGE),static_cast<std::uint64_t>(axis.group),mask);
            value=foldDigit(value,static_cast<std::uint64_t>(world::CHUNKS_PER_GROUP_EDGE),static_cast<std::uint64_t>(axis.chunk),mask);
            value=foldDigit(value,static_cast<std::uint64_t>(world::BLOCKS_PER_CHUNK_EDGE),static_cast<std::uint64_t>(axis.block),mask);return value;
        }
        long double u128ToLongDouble(U128 value)noexcept{const std::uint64_t lo=static_cast<std::uint64_t>(value),hi=static_cast<std::uint64_t>(value>>64u);return std::ldexp(static_cast<long double>(hi),64)+static_cast<long double>(lo);}
        double coefficientPhase(const Axis&axis,double coefficient)
        {
            const BinaryDouble binary=decomposeDouble(coefficient);if(binary.mantissa==0u)return 0.0;const int fractionalBits=binary.exponent2<0?-binary.exponent2:0;const int phaseBits=LATTICE_PERIOD_BITS+fractionalBits;if(phaseBits>=128)throw std::invalid_argument("noise scale is too tiny for the exact uint128 phase mapper");const U128 mask=maskBits(phaseBits);U128 factor=static_cast<U128>(binary.mantissa);if(binary.exponent2>0){if(binary.exponent2>=phaseBits)factor=0u;else factor=(factor<<binary.exponent2)&mask;}U128 product=(axisModuloPow2(axis,phaseBits)*factor)&mask;if(binary.negative)product=(static_cast<U128>(0u)-product)&mask;const long double scaled=std::ldexp(u128ToLongDouble(product),-fractionalBits);return wrapLattice(static_cast<double>(scaled));
        }
        double sumPhase(double a,double b)noexcept{return wrapLattice(a+b);} double sumPhase(double a,double b,double c)noexcept{return wrapLattice(a+b+c);}
        struct GroupAxisDivision{Axis quotient{};std::int64_t remainder=0;};
