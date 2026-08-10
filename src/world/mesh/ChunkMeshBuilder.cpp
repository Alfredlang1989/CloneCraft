#include "world/mesh/ChunkMeshBuilder.h"

#include "world/chunk/ChunkManager.h"
#include "world/coordinates/Coords.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <vector>

namespace world
{
    namespace
    {
        constexpr std::int32_t EDGE = static_cast<std::int32_t>( BLOCKS_PER_CHUNK_EDGE );
        constexpr std::size_t SLICE_AREA = static_cast<std::size_t>( EDGE ) * EDGE;

        struct Dir
        {
            std::int8_t sliceAxis;
            std::int8_t uAxis;
            std::int8_t vAxis;
            std::int8_t sign;
            std::uint8_t pattern;
        };

        constexpr Dir DIRS[6] = {
            { 0, 1, 2, 1, 0 },  // +X : u=y, v=z
            { 0, 1, 2, -1, 1 }, // -X : u=y, v=z
            { 1, 0, 2, 1, 1 },  // +Y : u=x, v=z
            { 1, 0, 2, -1, 0 }, // -Y : u=x, v=z
            { 2, 0, 1, 1, 0 },  // +Z : u=x, v=y
            { 2, 0, 1, -1, 1 }, // -Z : u=x, v=y
        };

        constexpr bool offsetU[2][4] = { { false, true, true, false },
                                         { false, false, true, true } };
        constexpr bool offsetV[2][4] = { { false, false, true, true },
                                         { false, true, true, false } };
    } // namespace

    ChunkMeshBuilder::ChunkMeshBuilder( const BlockIdTable &table,
                                        const BlockRegistry &blocks )
        : mTable( table ), mOpaque( table.size(), 0u ), mNeedsTangent( table.size(), 0u ),
          mCrossShape( table.size(), 0u )
    {
        for( std::size_t i = 1; i < mTable.size(); ++i )
        {
            const BlockDef &def = blocks.get( mTable.idOf( static_cast<std::uint16_t>( i ) ) );
            mOpaque[i] = def.opaque ? 1u : 0u;
            mNeedsTangent[i] = def.normalMap.empty() ? 0u : 1u;
            mCrossShape[i] = def.renderShape == BlockRenderShape::Cross ? 1u : 0u;
        }
    }

    void ChunkMeshBuilder::build( const ChunkManager &world, const ChunkAddress &chunk,
                                  ChunkMesh &mesh ) const
    {
        mesh.vertices.clear();

        const Chunk *data = world.chunkAt( chunk );
        if( !data || data->empty() )
            return;

        // A modest surface-based reserve avoids repeated vector growth for
        // normal terrain/caves without reserving the pathological voxel-face
        // worst case. The vectors retain this capacity across rebuilds.
        mesh.vertices.reserve( SLICE_AREA * 6u * 4u );

        const BlockAddress origin = chunkOrigin( chunk );

        auto emitQuad = [&mesh, this]( std::uint16_t blockId,
                                 const std::int32_t pos[4][3],
                                 const std::int8_t n[3],
                                 const float uv[4][2] ) {
            // Tangent generation is only required by materials that actually
            // have a normal map. Most terrain blocks do not; skipping the
            // gradient/sqrt work removes it from the hot meshing path.
            float tangent[3] = { 1.0f, 0.0f, 0.0f };
            const bool needsTangent =
                blockId < mNeedsTangent.size() && mNeedsTangent[blockId] != 0u;
            if( needsTangent )
            {
                const float e1[3] = {
                    static_cast<float>( pos[1][0] - pos[0][0] ),
                    static_cast<float>( pos[1][1] - pos[0][1] ),
                    static_cast<float>( pos[1][2] - pos[0][2] )
                };
                const float e2[3] = {
                    static_cast<float>( pos[3][0] - pos[0][0] ),
                    static_cast<float>( pos[3][1] - pos[0][1] ),
                    static_cast<float>( pos[3][2] - pos[0][2] )
                };
                const float du1 = uv[1][0] - uv[0][0];
                const float dv1 = uv[1][1] - uv[0][1];
                const float du2 = uv[3][0] - uv[0][0];
                const float dv2 = uv[3][1] - uv[0][1];
                const float determinant = du1 * dv2 - du2 * dv1;

                if( std::fabs( determinant ) > 1e-8f )
                {
                    const float inv = 1.0f / determinant;
                    tangent[0] = ( e1[0] * dv2 - e2[0] * dv1 ) * inv;
                    tangent[1] = ( e1[1] * dv2 - e2[1] * dv1 ) * inv;
                    tangent[2] = ( e1[2] * dv2 - e2[2] * dv1 ) * inv;
                    const float lenSq = tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                        tangent[2] * tangent[2];
                    if( lenSq > 1e-12f )
                    {
                        const float invLen = 1.0f / std::sqrt( lenSq );
                        tangent[0] *= invLen;
                        tangent[1] *= invLen;
                        tangent[2] *= invLen;
                    }
                }
            }

            for( int c = 0; c < 4; ++c )
            {
                MeshVertex vertex;
                vertex.x = static_cast<float>( pos[c][0] );
                vertex.y = static_cast<float>( pos[c][1] );
                vertex.z = static_cast<float>( pos[c][2] );
                vertex.nx = static_cast<float>( n[0] );
                vertex.ny = static_cast<float>( n[1] );
                vertex.nz = static_cast<float>( n[2] );
                vertex.tx = tangent[0];
                vertex.ty = tangent[1];
                vertex.tz = tangent[2];
                vertex.u = uv[c][0];
                vertex.v = uv[c][1];
                vertex.blockId = blockId;
                mesh.vertices.push_back( vertex );
            }
        };

        // Fixed-size per-slice scratch lives on the stack. At EDGE=32 this is
        // only 3 KiB and removes two heap allocations from every chunk build.
        std::array<std::uint16_t, SLICE_AREA> grid{};
        std::array<std::uint8_t, SLICE_AREA> taken{};
        const auto cell = []( std::int32_t u, std::int32_t v ) -> std::size_t {
            return static_cast<std::size_t>( u ) * static_cast<std::size_t>( EDGE ) +
                   static_cast<std::size_t>( v );
        };

        for( const Dir &d : DIRS )
        {
            for( std::int32_t s = 0; s < EDGE; ++s )
            {
                taken.fill( 0u );
                for( std::int32_t u = 0; u < EDGE; ++u )
                {
                    for( std::int32_t v = 0; v < EDGE; ++v )
                    {
                        const std::size_t idx = cell( u, v );

                        const std::int32_t local[3] = {
                            d.sliceAxis == 0 ? s : d.uAxis == 0 ? u : v,
                            d.sliceAxis == 1 ? s : d.uAxis == 1 ? u : v,
                            d.sliceAxis == 2 ? s : d.uAxis == 2 ? u : v
                        };
                        const std::uint16_t blockId = data->block( local[0], local[1], local[2] );
                        if( blockId == 0 ||
                            ( blockId < mCrossShape.size() && mCrossShape[blockId] != 0u ) )
                        {
                            // Cross-shaped decoration blocks are emitted in a
                            // dedicated pass below and never participate in cube
                            // face occlusion/greedy merging.
                            grid[idx] = 0;
                            continue;
                        }

                        const std::int64_t nLocal[3] = {
                            local[0] + ( d.sliceAxis == 0 ? d.sign : 0 ),
                            local[1] + ( d.sliceAxis == 1 ? d.sign : 0 ),
                            local[2] + ( d.sliceAxis == 2 ? d.sign : 0 )
                        };
                        std::uint16_t nbrId = 0;
                        bool neighborLoaded = true;

                        // More than 93% of face-neighbour checks for EDGE=16
                        // stay inside the same chunk. Read those directly from
                        // the dense voxel array instead of doing group-address +
                        // std::map + unordered_map lookups through ChunkManager.
                        if( nLocal[0] >= 0 && nLocal[0] < EDGE &&
                            nLocal[1] >= 0 && nLocal[1] < EDGE &&
                            nLocal[2] >= 0 && nLocal[2] < EDGE )
                        {
                            nbrId = data->block( static_cast<std::int32_t>( nLocal[0] ),
                                                 static_cast<std::int32_t>( nLocal[1] ),
                                                 static_cast<std::int32_t>( nLocal[2] ) );
                        }
                        else
                        {
                            const BlockAddress nbr = offsetBlock(
                                origin, nLocal[0], nLocal[1], nLocal[2] );
                            const auto nbrBlock = world.tryBlockAt( nbr );
                            if( nbrBlock.has_value() )
                                nbrId = *nbrBlock;
                            else
                                neighborLoaded = false;
                        }

                        if( !neighborLoaded )
                        {
                            // Provisional outer-rim face. When this neighbour
                            // loads, ChunkManager invalidates this chunk and the
                            // face is either kept (AIR) or culled (solid).
                            grid[idx] = blockId;
                            continue;
                        }

                        if( nbrId != 0 )
                        {
                            const bool neighborOpaque =
                                nbrId < mOpaque.size() ? mOpaque[nbrId] != 0 : true;
                            if( neighborOpaque || nbrId == blockId )
                            {
                                grid[idx] = 0;
                                continue;
                            }
                        }
                        grid[idx] = blockId;
                    }
                }

                for( std::int32_t u = 0; u < EDGE; ++u )
                {
                    for( std::int32_t v = 0; v < EDGE; ++v )
                    {
                        const std::size_t start = cell( u, v );
                        if( taken[start] || grid[start] == 0 )
                            continue;
                        const std::uint16_t id = grid[start];

                        std::int32_t w = 1;
                        while( v + w < EDGE && !taken[cell( u, v + w )] &&
                               grid[cell( u, v + w )] == id )
                            ++w;

                        std::int32_t h = 1;
                        bool canExtend = true;
                        while( u + h < EDGE && canExtend )
                        {
                            for( std::int32_t k = 0; k < w; ++k )
                            {
                                if( taken[cell( u + h, v + k )] ||
                                    grid[cell( u + h, v + k )] != id )
                                {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if( canExtend )
                                ++h;
                        }

                        // Local texture coordinates. A merged h x w quad
                        // intentionally spans h x w UV units. Because each
                        // block has its own texture/material, TAM_WRAP repeats
                        // that one texture instead of walking through an atlas.
                        const float uStart = 0.0f;
                        const float vStart = 0.0f;
                        const float uEnd = static_cast<float>( h );
                        const float vEnd = static_cast<float>( w );

                        const std::int32_t plane = s + ( d.sign > 0 ? 1 : 0 );

                        std::int32_t pos[4][3];
                        float uv[4][2];
                        for( int c = 0; c < 4; ++c )
                        {
                            const std::int32_t cornerU =
                                u + ( offsetU[d.pattern][c] ? h : 0 );
                            const std::int32_t cornerV =
                                v + ( offsetV[d.pattern][c] ? w : 0 );
                            pos[c][d.sliceAxis] = plane;
                            pos[c][d.uAxis] = cornerU;
                            pos[c][d.vAxis] = cornerV;
                            uv[c][0] = offsetU[d.pattern][c] ? uEnd : uStart;
                            uv[c][1] = offsetV[d.pattern][c] ? vEnd : vStart;
                        }
                        const std::int8_t n[3] = {
                            static_cast<std::int8_t>( d.sliceAxis == 0 ? d.sign : 0 ),
                            static_cast<std::int8_t>( d.sliceAxis == 1 ? d.sign : 0 ),
                            static_cast<std::int8_t>( d.sliceAxis == 2 ? d.sign : 0 )
                        };
                        emitQuad( id, pos, n, uv );

                        for( std::int32_t du = 0; du < h; ++du )
                            for( std::int32_t dv = 0; dv < w; ++dv )
                                taken[cell( u + du, v + dv )] = 1;
                    }
                }
            }
        }

        // Data-driven crossed texture planes for grass/flowers/reeds. Emit both
        // windings because the normal PBS material keeps ordinary back-face
        // culling; four quads per voxel therefore render the X from either side.
        const auto emitCrossQuad = [&mesh]( std::uint16_t blockId,
                                            const float pos[4][3],
                                            const float normal[3] ) {
            for( int c = 0; c < 4; ++c )
            {
                MeshVertex vertex;
                vertex.x = pos[c][0]; vertex.y = pos[c][1]; vertex.z = pos[c][2];
                vertex.nx = normal[0]; vertex.ny = normal[1]; vertex.nz = normal[2];
                vertex.u = ( c == 1 || c == 2 ) ? 1.0f : 0.0f;
                vertex.v = ( c >= 2 ) ? 0.0f : 1.0f;
                vertex.blockId = blockId;
                mesh.vertices.push_back( vertex );
            }
        };

        constexpr float INV_SQRT2 = 0.70710678118f;
        for( std::int32_t x = 0; x < EDGE; ++x )
            for( std::int32_t y = 0; y < EDGE; ++y )
                for( std::int32_t z = 0; z < EDGE; ++z )
                {
                    const std::uint16_t id = data->block( x, y, z );
                    if( id == 0 || id >= mCrossShape.size() || mCrossShape[id] == 0u )
                        continue;
                    const float fx = static_cast<float>( x );
                    const float fy = static_cast<float>( y );
                    const float fz = static_cast<float>( z );

                    const float p0[4][3] = {
                        { fx,        fy,        fz },
                        { fx + 1.0f, fy,        fz + 1.0f },
                        { fx + 1.0f, fy + 1.0f, fz + 1.0f },
                        { fx,        fy + 1.0f, fz }
                    };
                    const float n0[3] = { INV_SQRT2, 0.0f, -INV_SQRT2 };
                    emitCrossQuad( id, p0, n0 );
                    const float p0Back[4][3] = {
                        { fx + 1.0f, fy,        fz + 1.0f },
                        { fx,        fy,        fz },
                        { fx,        fy + 1.0f, fz },
                        { fx + 1.0f, fy + 1.0f, fz + 1.0f }
                    };
                    const float n0Back[3] = { -INV_SQRT2, 0.0f, INV_SQRT2 };
                    emitCrossQuad( id, p0Back, n0Back );

                    const float p1[4][3] = {
                        { fx + 1.0f, fy,        fz },
                        { fx,        fy,        fz + 1.0f },
                        { fx,        fy + 1.0f, fz + 1.0f },
                        { fx + 1.0f, fy + 1.0f, fz }
                    };
                    const float n1[3] = { INV_SQRT2, 0.0f, INV_SQRT2 };
                    emitCrossQuad( id, p1, n1 );
                    const float p1Back[4][3] = {
                        { fx,        fy,        fz + 1.0f },
                        { fx + 1.0f, fy,        fz },
                        { fx + 1.0f, fy + 1.0f, fz },
                        { fx,        fy + 1.0f, fz + 1.0f }
                    };
                    const float n1Back[3] = { -INV_SQRT2, 0.0f, -INV_SQRT2 };
                    emitCrossQuad( id, p1Back, n1Back );
                }
    }
} // namespace world
