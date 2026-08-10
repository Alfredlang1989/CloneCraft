#pragma once

#include <cstdint>

namespace worldgen
{
    inline std::uint64_t mixSeed64( std::uint64_t v )
    {
        v += 0x9e3779b97f4a7c15ULL;
        v = ( v ^ ( v >> 30 ) ) * 0xbf58476d1ce4e5b9ULL;
        v = ( v ^ ( v >> 27 ) ) * 0x94d049bb133111ebULL;
        return v ^ ( v >> 31 );
    }

    inline std::uint64_t deriveNoiseSeed( std::uint64_t seed, std::uint64_t salt )
    {
        return mixSeed64( mixSeed64( seed ) ^ salt );
    }
} // namespace worldgen
