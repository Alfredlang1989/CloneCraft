#pragma once

#include "world/coordinates/Coords.h"
#include "world/worldgen/WorldGenConfig.h"

#include <cstdint>
#include <string>
#include <vector>

namespace worldgen
{
    struct SampledField
    {
        std::string id;
        FieldDimension dimension = FieldDimension::D3;
        std::vector<double> values;

        double at2D( std::int64_t lx, std::int64_t lz ) const;
        double at3D( std::int64_t lx, std::int64_t ly, std::int64_t lz ) const;
    };

    class LuaFieldEvaluator
    {
    public:
        LuaFieldEvaluator( const FieldConfig &config, std::uint64_t worldSeed );

        const FieldConfig &config() const { return mConfig; }
        SampledField sampleChunk( const world::ChunkAddress &chunk ) const;

        /** Fill one canonical X slice of a pre-sized sampled field. The caller may
         * invoke distinct slices concurrently; each worker uses its own Lua VM. */
        void sampleChunkXSlice( const world::ChunkAddress &chunk, std::int64_t lx,
                                SampledField &sampled ) const;
        double sample2D( const world::BlockAddress &point ) const;
        double sample3D( const world::BlockAddress &point ) const;

        // Structure scripts use only local relative integer coordinates. They
        // are intentionally separate from the world-field API.
        double sample3DWithSeed( std::int64_t x, std::int64_t y, std::int64_t z,
                                 std::uint64_t sampleSeed ) const;

    private:
        FieldConfig mConfig;
        std::uint64_t mWorldSeed = 0;
        std::uint64_t mInstanceId = 0;
        std::string mScriptSource;
    };
} // namespace worldgen
