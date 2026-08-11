#pragma once

#include <compare>
#include <cstdint>

namespace spatial::dynamic
{
    struct Position3f
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        friend constexpr auto operator<=>( const Position3f &, const Position3f & ) = default;
    };

    struct RebaseDelta
    {
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;

        constexpr bool any() const noexcept { return x != 0 || y != 0 || z != 0; }
        friend constexpr auto operator<=>( const RebaseDelta &, const RebaseDelta & ) = default;
    };

    /**
     * Local floating coordinate policy for dynamic objects.
     *
     * This space is intentionally independent from Chunk/ChunkGroup/Section
     * world-address radices. The default 65536-block edge gives a local range
     * of [-32768,+32768) and therefore only requires a rebase after very long
     * travel. Rebase deltas are whole blocks and are applied to every active
     * dynamic object together.
     */
    class DynamicSpace
    {
    public:
        static constexpr std::int64_t kDefaultEdgeBlocks = 65536;

        explicit DynamicSpace( std::int64_t edgeBlocks = kDefaultEdgeBlocks );

        std::int64_t edgeBlocks() const noexcept { return mEdgeBlocks; }
        double halfEdgeBlocks() const noexcept { return static_cast<double>( mEdgeBlocks ) * 0.5; }

        RebaseDelta rebaseDeltaFor( const Position3f &position ) const;
        bool needsRebase( const Position3f &position ) const { return rebaseDeltaFor( position ).any(); }
        void applyRebase( Position3f &position, const RebaseDelta &delta ) const;

    private:
        std::int64_t axisRebaseDelta( float coordinate ) const;
        std::int64_t mEdgeBlocks;
    };
} // namespace spatial::dynamic
