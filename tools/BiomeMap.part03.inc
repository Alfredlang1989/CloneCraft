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
                  << "extent: " << args.width << " x " << args.height << " pixels"
                  << " = " << static_cast<std::int64_t>( args.width ) * args.blocksPerPixel << " x "
                  << static_cast<std::int64_t>( args.height ) * args.blocksPerPixel << " blocks\n"
                  << "blocks per pixel: " << args.blocksPerPixel << "\n"
                  << "center chunk: " << args.centerChunkX << ", " << args.centerChunkZ << "\n"
                  << "mode: " << ( args.blend ? "blend" : "dominant" ) << "\n\n";

        for( std::size_t i = 0; i < explicitBiomes.size(); ++i )
        {
            const auto n = counts[i].load();
            std::cout << std::setw( 28 ) << std::left << explicitBiomes[i].biome->id
                      << std::right << std::setw( 10 ) << n << "  "
                      << std::fixed << std::setprecision( 2 ) << ( n * 100.0 / total ) << "%\n";
        }
        {
            const auto n = counts[explicitBiomes.size()].load();
            std::cout << std::setw( 28 ) << std::left << fallbackBiome->id
                      << std::right << std::setw( 10 ) << n << "  "
                      << std::fixed << std::setprecision( 2 ) << ( n * 100.0 / total ) << "%\n";
        }
        std::cout << "\nnormalized terrain contribution (same mask normalization as WorldGen):\n";
        for( std::size_t i = 0; i < explicitBiomes.size(); ++i )
            std::cout << "  " << std::setw( 26 ) << std::left << explicitBiomes[i].biome->id
                      << std::right << std::fixed << std::setprecision( 2 )
                      << ( contributionSums[i] * 100.0 / total ) << "%\n";
        std::cout << "  " << std::setw( 26 ) << std::left << fallbackBiome->id
                  << std::right << std::fixed << std::setprecision( 2 )
                  << ( contributionSums[explicitBiomes.size()] * 100.0 / total ) << "%\n";

        const auto rivers = counts[explicitBiomes.size() + 1u].load();
        const auto snow = counts[explicitBiomes.size() + 2u].load();
        const auto oceans = counts[explicitBiomes.size() + 3u].load();
        const auto lakes = counts[explicitBiomes.size() + 4u].load();
        const double oceanPct = oceans * 100.0 / total;
        const double lakePct = lakes * 100.0 / total;
        const double dryLandPct = 100.0 - oceanPct - lakePct;
        std::cout << "\nwater coverage:\n"
                  << "  ocean:       " << std::fixed << std::setprecision( 2 ) << oceanPct << "%\n"
                  << "  inland lakes:" << std::fixed << std::setprecision( 2 ) << lakePct << "%\n"
                  << "  dry land:    " << std::fixed << std::setprecision( 2 ) << dryLandPct << "%\n"
                  << "river overlay (<0.03): " << rivers << "  "
                  << std::fixed << std::setprecision( 2 ) << ( rivers * 100.0 / total ) << "%\n"
                  << "snow overlay (>0.57):  " << snow << "  "
                  << std::fixed << std::setprecision( 2 ) << ( snow * 100.0 / total ) << "%\n";
        const std::int64_t extentX = static_cast<std::int64_t>( args.width ) * args.blocksPerPixel;
        const std::int64_t extentZ = static_cast<std::int64_t>( args.height ) * args.blocksPerPixel;
        const bool macroCoverageSample = std::min( extentX, extentZ ) >= 131072;
        if( macroCoverageSample && ( oceanPct < 45.0 || oceanPct > 75.0 ) )
            std::cout << "WARNING: ocean coverage outside diagnostic 45-75% macro guard band\n";
        if( macroCoverageSample && lakePct > 6.0 )
            std::cout << "WARNING: inland-water coverage above 6% macro guard band\n";

        return 0;
    }
    catch( const std::exception &e )
    {
        std::cerr << "biomemap: " << e.what() << "\n";
        return 1;
    }
}
