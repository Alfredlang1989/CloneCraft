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
        std::int64_t blocksPerPixel = world::BLOCKS_PER_CHUNK_EDGE;
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
            else if( arg == "--blocks-per-pixel" ) a.blocksPerPixel = std::stoll( need( i, "--blocks-per-pixel" ) );
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
                std::cout << "CloneCraft biome-map diagnostic renderer\n"
                          << "  --data DIR       data directory (default: data)\n"
                          << "  --output FILE    PNG output (default: biome_map.png)\n"
                          << "  --width N        pixels/chunks east-west (default: 1024)\n"
                          << "  --height N       pixels/chunks north-south (default: 1024)\n"
                          << "  --center-x N     center chunk X (default: 0)\n"
                          << "  --center-z N     center chunk Z (default: 0)\n"
                          << "  --blocks-per-pixel N  map sampling stride in blocks; multiple of chunk edge (default: 16)\n"
                          << "  --threads N      render workers\n"
                          << "  --mode MODE      dominant or blend (default: dominant)\n";
                std::exit( 0 );
            }
            else throw std::invalid_argument( "unknown argument: " + arg );
        }
        if( a.width < 1 || a.height < 1 || a.width > 16384 || a.height > 16384 )
            throw std::invalid_argument( "map size must be in 1..16384" );
        if( a.blocksPerPixel < world::BLOCKS_PER_CHUNK_EDGE ||
            a.blocksPerPixel % world::BLOCKS_PER_CHUNK_EDGE != 0 )
            throw std::invalid_argument( "--blocks-per-pixel must be a positive multiple of the chunk edge" );
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

        std::vector<BiomeSampler> explicitBiomes;
        const world::BiomeDef *fallbackBiome = nullptr;
        for( const std::string &id : biomes.ids() )
        {
            const world::BiomeDef &biome = biomes.get( id );
            if( biome.terrainMaskField.empty() )
            {
                if( fallbackBiome ) throw std::runtime_error( "multiple fallback biomes" );
                fallbackBiome = &biome;
            }
            else
            {
                explicitBiomes.push_back( { &biome, ensureField( biome.terrainMaskField ), biomeColor( biome.id ) } );
            }
        }
        if( !fallbackBiome ) throw std::runtime_error( "no fallback biome found" );

        const auto *riverField = ensureField( "river_mask" );
        const auto *snowField = ensureField( "snow_mask" );
        const auto *oceanField = ensureField( "ocean_mask" );
        const auto *lakeField = ensureField( "lake_mask" );

        std::vector<std::uint8_t> image( static_cast<std::size_t>( args.width ) * args.height * 3u );
        std::vector<std::atomic<std::uint64_t>> counts( explicitBiomes.size() + 5u );
        // explicit biome counts, fallback, river, snow-overlay, ocean-water, lake-water
