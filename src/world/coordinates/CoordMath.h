#pragma once

#include <cstdint>

namespace world
{
    /**
     * floor_div(a, b) with b > 0: division rounding towards negative
     * infinity. Unlike C++ integer division (which truncates towards
     * zero), floor_div keeps coordinates consistent for negative values.
     *
     * floor_mod(a, b) is the matching remainder in range [0, b).
     *
     * Identities (for b > 0):
     *   floor_div(a, b) * b + floor_mod(a, b) == a
     *   floor_mod(a, b) >= 0
     */
    constexpr std::int64_t floorDiv( std::int64_t a, std::int64_t b )
    {
        std::int64_t q = a / b;
        std::int64_t r = a % b;
        if( r < 0 )
            --q;
        return q;
    }

    constexpr std::int64_t floorMod( std::int64_t a, std::int64_t b )
    {
        std::int64_t r = a % b;
        if( r < 0 )
            r += b;
        return r;
    }
} // namespace world