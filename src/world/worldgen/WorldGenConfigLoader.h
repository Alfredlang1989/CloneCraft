#pragma once

#include "world/worldgen/WorldGenConfig.h"

#include <filesystem>

namespace worldgen
{
    /** Loads and strictly validates world-generation tuning from JSON. */
    WorldGenConfig loadWorldGenConfig( const std::filesystem::path &path );
} // namespace worldgen
