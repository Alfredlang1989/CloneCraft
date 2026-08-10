#pragma once

#include "world/coordinates/Coords.h"

namespace world
{
    struct RelativePosition3f
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        friend constexpr auto operator<=>( const RelativePosition3f &, const RelativePosition3f & ) = default;
    };

    /**
     * Precision-safe continuous position. The integer cell is a hierarchical
     * BlockAddress; only the sub-block fractions use floating point.
     */
    class WorldPosition
    {
    public:
        WorldPosition() = default;

        static WorldPosition fromBlockAddress( const BlockAddress &cell,
                                               float fractionX = 0.0f,
                                               float fractionY = 0.0f,
                                               float fractionZ = 0.0f );

        static WorldPosition fromHierarchical( const GroupAddress &group,
                                               const LocalChunkCoord &chunk,
                                               const LocalBlockCoord &block,
                                               float fractionX = 0.0f,
                                               float fractionY = 0.0f,
                                               float fractionZ = 0.0f );

        const BlockAddress &blockAddress() const { return mCell; }
        const ChunkAddress &chunkAddress() const { return mCell.chunk; }
        const GroupAddress &group() const { return mCell.chunk.group; }
        const LocalBlockCoord &localBlock() const { return mCell.block; }

        float fractionX() const { return mFractionX; }
        float fractionY() const { return mFractionY; }
        float fractionZ() const { return mFractionZ; }

        float groupLocalX() const;
        float groupLocalY() const;
        float groupLocalZ() const;

        RelativePosition3f relativeToGroup( const GroupAddress &origin ) const;
        RelativeI64 blockRelativeToGroup( const GroupAddress &origin ) const;

        void translate( double dx, double dy, double dz );

    private:
        static void splitDelta( double currentFraction, double delta,
                                std::int64_t &whole, float &fraction );

        BlockAddress mCell{};
        float mFractionX = 0.0f;
        float mFractionY = 0.0f;
        float mFractionZ = 0.0f;
    };
} // namespace world
