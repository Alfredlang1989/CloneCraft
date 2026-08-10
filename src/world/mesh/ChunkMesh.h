#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace world
{
    /**
     * CPU-side chunk mesh: four sequential vertices per greedy quad.
     * This type is renderer-independent (no Ogre/SDL types); the renderer
     * layer translates it into GPU geometry.
     */
    struct MeshVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        // Tangent-space basis for normal maps. Bitangent is reconstructed in
        // the shader from normal x tangent, so one tangent vector is enough.
        float tx = 1.0f;
        float ty = 0.0f;
        float tz = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint16_t blockId = 0; // material/texture selection in renderer
    };

    struct ChunkMesh
    {
        // Greedy quads are stored as four sequential vertices per face. The
        // Ogre upload path expands each quad to two triangles; keeping a second
        // CPU index vector duplicated that topology without ever being consumed.
        std::vector<MeshVertex> vertices;

        std::size_t quadCount() const { return vertices.size() / 4u; }
    };
} // namespace world