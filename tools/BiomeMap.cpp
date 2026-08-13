#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"
#include "world/registry/BlockIdTable.h"
#include "world/registry/RegistryLoader.h"
#include "world/worldgen/LuaFieldEvaluator.h"
#include "world/worldgen/WorldGenConfigLoader.h"
#include "world/worldgen/WorldGen.h"

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
        compressed.resize( compressedSize );

        std::ofstream out( path, std::ios::binary );
        if( !out ) throw std::runtime_error( "cannot open PNG output: " + path.string() );
        const std::uint8_t signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        out.write( reinterpret_cast<const char *>( signature ), 8 );

        std::vector<std::uint8_t> ihdr( 13, 0 );
        auto put32 = [&]( int offset, std::uint32_t v ) {
            ihdr[offset + 0] = static_cast<std::uint8_t>( ( v >> 24u ) & 0xffu );
            ihdr[offset + 1] = static_cast<std::uint8_t>( ( v >> 16u ) & 0xffu );
            ihdr[offset + 2] = static_cast<std::uint8_t>( ( v >> 8u ) & 0xffu );
            ihdr[offset + 3] = static_cast<std::uint8_t>( v & 0xffu );
        };
        put32( 0, static_cast<std::uint32_t>( width ) );
        put32( 4, static_cast<std::uint32_t>( height ) );
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 2;  // RGB
        writeChunk( out, "IHDR", ihdr );
        writeChunk( out, "IDAT", compressed );
        writeChunk( out, "IEND", {} );
    }

    struct Args
    {
        std::filesystem::path dataDir = "data";
        std::filesystem::path output = "biome_map.png";
        int width = 1024;
        int height = 1024;
        std::int64_t centerChunkX = 0;
        std::int64_t centerChunkZ = 0;
        unsigned threads = std::max( 1u, std::thread::hardware_concurrency() );
        bool blend = false;
    };

    Args parseArgs( int argc, char **argv )
    {
        Args a;
        auto need = [&]( int &i, const char *name ) -> std::string {
            if( i + 1 >= argc ) throw std::invalid_argument( std::string( "missing value for " ) + name );
            return argv[++i];
        };
        for( int i = 1; i < argc; ++i )
        {
            const std::string arg = argv[i];
            if( arg == "--data" ) a.dataDir = need( i, "--data" );
            else if( arg == "--output" ) a.output = need( i, "--output" );
            else if( arg == "--width" ) a.width = std::stoi( need( i, "--width" ) );
            else if( arg == "--height" ) a.height = std::stoi( need( i, "--height" ) );
            else if( arg == "--center-x" ) a.centerChunkX = std::stoll( need( i, "--center-x" ) );
            else if( arg == "--center-z" ) a.centerChunkZ = std::stoll( need( i, "--center-z" ) );
            else if( arg == "--threads" ) a.threads = static_cast<unsigned>( std::stoul( need( i, "--threads" ) ) );
            else if( arg == "--mode" )
            {
                const std::string mode = need( i, "--mode" );
                if( mode == "dominant" ) a.blend = false;
                else if( mode == "blend" ) a.blend = true;
                else throw std::invalid_argument( "--mode must be dominant or blend" );
            }
            else if( arg == "--help" )
            {
                std::cout << "OmniGrid biome-map diagnostic renderer\n"
                          << "  --data DIR       data directory (default: data)\n"
                          << "  --output FILE    PNG output (default: biome_map.png)\n"
                          << "  --width N        pixels/chunks east-west (default: 1024)\n"
                          << "  --height N       pixels/chunks north-south (default: 1024)\n"
                          << "  --center-x N     center chunk X (default: 0)\n"
                          << "  --center-z N     center chunk Z (default: 0)\n"
                          << "  --threads N      render workers\n"
                          << "  --mode MODE      dominant or blend (default: dominant)\n";
                std::exit( 0 );
            }
            else throw std::invalid_argument( "unknown argument: " + arg );
        }
        if( a.width < 1 || a.height < 1 || a.width > 16384 || a.height > 16384 )
            throw std::invalid_argument( "map size must be in 1..16384" );
        if( a.threads == 0 ) a.threads = 1;
        return a;
    }
}

int main( int argc, char **argv )
{
    try
    {
        const Args args = parseArgs( argc, argv );
        world::BlockRegistry blocks;
        world::BiomeRegistry biomes;
        world::ResourceRegistry resources;
        world::RegistryLoader::loadFromDirectory( args.dataDir, blocks, biomes, resources );

        const auto cfg = worldgen::loadWorldGenConfig( args.dataDir / "worldgen.json" );

        std::unordered_map<std::string, std::unique_ptr<worldgen::LuaFieldEvaluator>> fields;
        auto ensureField = [&]( const std::string &id ) -> const worldgen::LuaFieldEvaluator * {
            if( id.empty() ) return nullptr;
            if( const auto it = fields.find( id ); it != fields.end() ) return it->second.get();
            const auto configIt = std::find_if( cfg.fields.begin(), cfg.fields.end(), [&]( const auto &f ) {
                return f.id == id;
            } );
            if( configIt == cfg.fields.end() ) throw std::runtime_error( "missing field '" + id + "'" );
            if( configIt->dimension != worldgen::FieldDimension::D2 )
                throw std::runtime_error( "diagnostic field '" + id + "' is not 2D" );
            auto ptr = std::make_unique<worldgen::LuaFieldEvaluator>( *configIt, cfg.seed );
            const auto *raw = ptr.get();
            fields.emplace( id, std::move( ptr ) );
            return raw;
        };

        world::BlockIdTable blockTable( blocks );
        worldgen::WorldGen generator( cfg, blocks, blockTable, biomes );

        std::vector<BiomeSampler> mapBiomes;
        std::unordered_map<std::string, std::size_t> biomeIndexById;
        for( const std::string &id : biomes.ids() )
        {
            const world::BiomeDef &biome = biomes.get( id );
            biomeIndexById.emplace( id, mapBiomes.size() );
            mapBiomes.push_back( { &biome, biomeColor( biome.id ) } );
        }
        if( mapBiomes.empty() ) throw std::runtime_error( "no biomes found" );

        const auto *riverField = ensureField( "river_mask" );
        const auto *snowField = ensureField( "snow_mask" );

        std::vector<std::uint8_t> image( static_cast<std::size_t>( args.width ) * args.height * 3u );
        std::vector<std::atomic<std::uint64_t>> counts( mapBiomes.size() + 2u );
        // biome dominance counts, river, snow-overlay
        for( auto &c : counts ) c.store( 0 );
        std::vector<double> contributionSums( mapBiomes.size(), 0.0 );
        std::mutex contributionMutex;

        std::atomic<int> nextRow{ 0 };
        std::atomic<int> finishedRows{ 0 };
        std::mutex printMutex;

        auto render = [&] {
            std::vector<double> localContributions( mapBiomes.size(), 0.0 );
            for( ;; )
            {
                const int py = nextRow.fetch_add( 1 );
                if( py >= args.height ) break;
                const std::int64_t chunkZ = args.centerChunkZ + static_cast<std::int64_t>( py ) - args.height / 2;
                for( int px = 0; px < args.width; ++px )
                {
                    const std::int64_t chunkX = args.centerChunkX + static_cast<std::int64_t>( px ) - args.width / 2;
                    const world::ChunkAddress chunk = world::offsetChunk( world::originChunkAddress(), chunkX, 0, chunkZ );
                    const world::BlockAddress point = world::blockAt(
                        chunk, { world::BLOCKS_PER_CHUNK_EDGE / 2, 0, world::BLOCKS_PER_CHUNK_EDGE / 2 } );

                    const std::vector<worldgen::BiomeWeightSample> resolved = generator.biomeWeights( point );
                    std::vector<double> biomeWeights( mapBiomes.size(), 0.0 );
                    double bestWeight = -1.0;
                    std::size_t best = 0;
                    for( const worldgen::BiomeWeightSample &sample : resolved )
                    {
                        const auto it = biomeIndexById.find( sample.id );
                        if( it == biomeIndexById.end() ) continue;
                        const std::size_t i = it->second;
                        biomeWeights[i] = sample.weight;
                        localContributions[i] += sample.weight;
                        if( sample.weight > bestWeight )
                        {
                            bestWeight = sample.weight;
                            best = i;
                        }
                    }

                    Rgb color{};
                    if( args.blend )
                    {
                        double rr = 0.0, gg = 0.0, bb = 0.0, totalWeight = 0.0;
                        for( std::size_t i = 0; i < mapBiomes.size(); ++i )
                        {
                            rr += biomeWeights[i] * mapBiomes[i].color.r;
                            gg += biomeWeights[i] * mapBiomes[i].color.g;
                            bb += biomeWeights[i] * mapBiomes[i].color.b;
                            totalWeight += biomeWeights[i];
                        }
                        const double denom = std::max( totalWeight, 1.0e-12 );
                        color = {
                            static_cast<std::uint8_t>( std::clamp( std::lround( rr / denom ), 0l, 255l ) ),
                            static_cast<std::uint8_t>( std::clamp( std::lround( gg / denom ), 0l, 255l ) ),
                            static_cast<std::uint8_t>( std::clamp( std::lround( bb / denom ), 0l, 255l ) )
                        };
                    }
                    else
                    {
                        color = mapBiomes[best].color;
                        color = mix( biomeColor( "core:plains" ), color,
                                     std::clamp( 0.45 + bestWeight * 0.75, 0.0, 1.0 ) );
                    }

                    bool river = false;
                    if( riverField )
                    {
                        const double d = riverField->sample2D( point );
                        river = d < 0.030; // exact current river_water pass threshold
                        if( river ) color = { 41, 103, 187 };
                    }

                    bool snow = false;
                    if( !river && snowField )
                    {
                        const double s = snowField->sample2D( point );
                        if( s > 0.57 )
                        {
                            snow = true;
                            const double t = std::clamp( ( s - 0.57 ) / 0.35, 0.25, 0.85 );
                            color = mix( color, { 238, 242, 240 }, t );
                        }
                    }

                    const std::size_t out = ( static_cast<std::size_t>( py ) * args.width + px ) * 3u;
                    image[out + 0] = color.r;
                    image[out + 1] = color.g;
                    image[out + 2] = color.b;

                    counts[best].fetch_add( 1, std::memory_order_relaxed );
                    if( river ) counts[mapBiomes.size()].fetch_add( 1, std::memory_order_relaxed );
                    if( snow ) counts[mapBiomes.size() + 1u].fetch_add( 1, std::memory_order_relaxed );
                }
                const int done = finishedRows.fetch_add( 1 ) + 1;
                if( done % std::max( 1, args.height / 20 ) == 0 || done == args.height )
                {
                    std::scoped_lock lock( printMutex );
                    std::cerr << "render " << std::setw( 3 )
                              << ( done * 100 / args.height ) << "%\r" << std::flush;
                }
            }
            {
                std::scoped_lock lock( contributionMutex );
                for( std::size_t i = 0; i < contributionSums.size(); ++i )
                    contributionSums[i] += localContributions[i];
            }
        };

        const unsigned workers = std::min<unsigned>( args.threads, static_cast<unsigned>( args.height ) );
        std::vector<std::thread> pool;
        pool.reserve( workers );
        for( unsigned i = 0; i < workers; ++i ) pool.emplace_back( render );
        for( auto &t : pool ) t.join();
        std::cerr << "\n";

        writePng( args.output, args.width, args.height, image );

        const double total = static_cast<double>( args.width ) * args.height;
        std::cout << "PNG: " << args.output << "\n"
                  << "seed: " << cfg.seed << "\n"
                  << "extent: " << args.width << " x " << args.height << " chunks"
                  << " = " << args.width * world::BLOCKS_PER_CHUNK_EDGE << " x "
                  << args.height * world::BLOCKS_PER_CHUNK_EDGE << " blocks\n"
                  << "center chunk: " << args.centerChunkX << ", " << args.centerChunkZ << "\n"
                  << "mode: " << ( args.blend ? "blend" : "dominant" ) << "\n\n";

        for( std::size_t i = 0; i < mapBiomes.size(); ++i )
        {
            const auto n = counts[i].load();
            std::cout << std::setw( 28 ) << std::left << mapBiomes[i].biome->id
                      << std::right << std::setw( 10 ) << n << "  "
                      << std::fixed << std::setprecision( 2 ) << ( n * 100.0 / total ) << "%\n";
        }
        std::cout << "\nnormalized terrain contribution (same resolver as WorldGen):\n";
        for( std::size_t i = 0; i < mapBiomes.size(); ++i )
            std::cout << "  " << std::setw( 26 ) << std::left << mapBiomes[i].biome->id
                      << std::right << std::fixed << std::setprecision( 2 )
                      << ( contributionSums[i] * 100.0 / total ) << "%\n";

        const auto rivers = counts[mapBiomes.size()].load();
        const auto snow = counts[mapBiomes.size() + 1u].load();
        std::cout << "\nriver overlay (<0.03): " << rivers << "  "
                  << std::fixed << std::setprecision( 2 ) << ( rivers * 100.0 / total ) << "%\n"
                  << "snow overlay (>0.57):  " << snow << "  "
                  << std::fixed << std::setprecision( 2 ) << ( snow * 100.0 / total ) << "%\n";

        return 0;
    }
    catch( const std::exception &e )
    {
        std::cerr << "biomemap: " << e.what() << "\n";
        return 1;
    }
}
