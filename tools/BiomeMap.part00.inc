#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"
#include "world/registry/RegistryLoader.h"
#include "world/worldgen/LuaFieldEvaluator.h"
#include "world/worldgen/WorldGenConfigLoader.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zlib.h>

namespace
{
    struct Rgb
    {
        std::uint8_t r = 0, g = 0, b = 0;
    };

    struct BiomeSampler
    {
        const world::BiomeDef *biome = nullptr;
        const worldgen::LuaFieldEvaluator *field = nullptr;
        Rgb color{};
    };

    std::string lower( std::string s )
    {
        std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) {
            return static_cast<char>( std::tolower( c ) );
        } );
        return s;
    }

    Rgb hashedColor( const std::string &id )
    {
        std::uint32_t h = 2166136261u;
        for( const unsigned char c : id )
        {
            h ^= c;
            h *= 16777619u;
        }
        return {
            static_cast<std::uint8_t>( 70u + ( h & 0x7fu ) ),
            static_cast<std::uint8_t>( 70u + ( ( h >> 8u ) & 0x7fu ) ),
            static_cast<std::uint8_t>( 70u + ( ( h >> 16u ) & 0x7fu ) )
        };
    }

    Rgb biomeColor( const std::string &id )
    {
        const std::string s = lower( id );
        if( s.find( "desert_high" ) != std::string::npos ) return { 178, 128, 62 };
        if( s.find( "high_mountain" ) != std::string::npos ) return { 148, 150, 151 };
        if( s.find( "alpine" ) != std::string::npos ) return { 105, 126, 106 };
        if( s.find( "badlands" ) != std::string::npos ) return { 174, 86, 43 };
        if( s.find( "desert" ) != std::string::npos ) return { 232, 205, 82 };
        if( s.find( "forest" ) != std::string::npos ) return { 27, 91, 39 };
        if( s.find( "rolling" ) != std::string::npos || s.find( "hill" ) != std::string::npos )
            return { 112, 151, 63 };
        if( s.find( "savanna" ) != std::string::npos || s.find( "savane" ) != std::string::npos )
            return { 142, 150, 66 };
        if( s.find( "mushroom" ) != std::string::npos || s.find( "pilz" ) != std::string::npos )
            return { 145, 83, 137 };
        if( s.find( "ocean" ) != std::string::npos || s.find( "sea" ) != std::string::npos )
            return { 37, 92, 164 };
        if( s.find( "plains" ) != std::string::npos || s.find( "grass" ) != std::string::npos )
            return { 75, 166, 69 };
        return hashedColor( id );
    }

    Rgb mix( Rgb a, Rgb b, double t )
    {
        t = std::clamp( t, 0.0, 1.0 );
        auto ch = [t]( std::uint8_t x, std::uint8_t y ) {
            return static_cast<std::uint8_t>( std::lround( x * ( 1.0 - t ) + y * t ) );
        };
        return { ch( a.r, b.r ), ch( a.g, b.g ), ch( a.b, b.b ) };
    }

    void writeU32be( std::ofstream &out, std::uint32_t v )
    {
        const std::uint8_t b[4] = {
            static_cast<std::uint8_t>( ( v >> 24u ) & 0xffu ),
            static_cast<std::uint8_t>( ( v >> 16u ) & 0xffu ),
            static_cast<std::uint8_t>( ( v >> 8u ) & 0xffu ),
            static_cast<std::uint8_t>( v & 0xffu )
        };
        out.write( reinterpret_cast<const char *>( b ), 4 );
    }

    void writeChunk( std::ofstream &out, const char type[4], const std::vector<std::uint8_t> &data )
    {
        writeU32be( out, static_cast<std::uint32_t>( data.size() ) );
        out.write( type, 4 );
        if( !data.empty() )
            out.write( reinterpret_cast<const char *>( data.data() ), static_cast<std::streamsize>( data.size() ) );

        uLong crc = crc32( 0L, Z_NULL, 0 );
        crc = crc32( crc, reinterpret_cast<const Bytef *>( type ), 4 );
        if( !data.empty() ) crc = crc32( crc, data.data(), static_cast<uInt>( data.size() ) );
        writeU32be( out, static_cast<std::uint32_t>( crc ) );
    }

    void writePng( const std::filesystem::path &path, int width, int height,
                   const std::vector<std::uint8_t> &rgb )
    {
        if( width <= 0 || height <= 0 ) throw std::invalid_argument( "invalid PNG size" );
        if( rgb.size() != static_cast<std::size_t>( width ) * height * 3u )
            throw std::invalid_argument( "RGB buffer size mismatch" );

        std::vector<std::uint8_t> raw;
        raw.resize( static_cast<std::size_t>( height ) * ( 1u + static_cast<std::size_t>( width ) * 3u ) );
        for( int y = 0; y < height; ++y )
        {
            const std::size_t dst = static_cast<std::size_t>( y ) * ( 1u + static_cast<std::size_t>( width ) * 3u );
            raw[dst] = 0; // PNG filter: None
            std::copy_n( rgb.data() + static_cast<std::size_t>( y ) * width * 3u,
                         static_cast<std::size_t>( width ) * 3u, raw.data() + dst + 1u );
        }

        uLongf compressedSize = compressBound( static_cast<uLong>( raw.size() ) );
        std::vector<std::uint8_t> compressed( compressedSize );
        const int rc = compress2( compressed.data(), &compressedSize, raw.data(),
                                  static_cast<uLong>( raw.size() ), Z_BEST_SPEED );
        if( rc != Z_OK ) throw std::runtime_error( "zlib compression failed" );
