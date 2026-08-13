# RENDERER

## Ownership

- SDL3 owns the native application window and polls events.
- OgreNext is the only renderer and is attached to the SDL-owned window.
- No `SDL_Renderer` exists.
- ESC / window close leaves the main loop and performs ordered cleanup.

## Current terrain render path

Terrain uses greedy meshing and OgreNext **HLMS PBS** materials, not packed-atlas UVs.

```text
Block JSON
   |
   +-- texture? -> Ogre file texture (sRGB input)
   +-- else color? -> JSON sRGB -> linear PBS background diffuse
   +-- else -> magenta/black diagnostic texture
   |
   +-- roughness / metalness / reflection / transparency
   +-- optional normalMap + normalMapStrength
   +-- optional reflectionMap cubemap
   +-- receiveShadows / castShadows

Chunk voxels
   -> visibility
   -> greedy quads
   -> position + normal + tangent + repeat UV
   -> grouped by runtime BlockId
   -> Ogre HLMS PBS material sections
```

A packed atlas is intentionally not used: `TAM_WRAP` repeats an entire GPU texture, not one atlas tile. Each block material therefore binds its own repeatable base texture and optional normal map.

## Daylight and shading

The prototype now has real directional lighting:

- daylight clear colour `#78A7FF`;
- warm directional sun;
- brighter upper-hemisphere sky fill + readable lower-hemisphere ground bounce;
- camera-mounted warm-white spotlight (flashlight), enabled by default;
- `F` toggles the flashlight without key-repeat strobing;
- Forward3D enabled for non-shadowed local PBS lights (flashlight now, torches later);
- Ogre PBS physically based shading;
- PCF 3x3 filtered PSSM sun shadows;
- three shadow splits (2048 + 1024 + 1024 atlas regions);
- shadow far distance currently 220 blocks.

The flashlight currently uses a 72-block attenuation radius and a 52-degree outer cone. It deliberately does **not** allocate its own shadow map yet; the sun remains the only dynamic shadow caster. This keeps the first local-light implementation cheap enough for the target Vega iGPU while still lighting cave interiors and dark terrain.

Terrain is split into at most two ManualObjects per chunk so `castShadows` can be honoured without creating an Ogre object per block material. `receiveShadows` is handled directly by the PBS datablock.

Cutout foliage is represented by `alphaMode: "mask"` plus `alphaCutoff`. The renderer maps this to OgreNext HLMS alpha testing without enabling alpha blending. Because the alpha test is also active in the shadow-caster shader, transparent leaf/plant texels do not behave like solid voxel shadow casters. `alphaMode: "blend"` remains reserved for genuinely transmissive materials such as water and glass.

## Block material data

See `docs/MATERIALS.md`. Core rule remains:

```text
texture -> color -> diagnostic fallback
```

PBR properties are loaded from `MODS/Default/blocks.json`; there is no hardcoded per-block material palette in the renderer.

## Transparency and refraction

PBS transparency is active. Omnigrid JSON uses `transparency=0` for opaque and `1` for fully transparent; Ogre PBS uses the inverse opacity convention, so the renderer converts it explicitly.

True Ogre screen-space refraction requires its own compositor pass. Refraction/IOR are already represented in the registry/material model, but the current workspace intentionally uses transparent PBS fallback until that dedicated pass exists. See `docs/MATERIALS.md`.

## Current CPU-side renderer optimisations

- `sync()` exits immediately if no chunks are dirty;
- all-AIR chunks are rejected by the mesher in O(1);
- dense runtime block ids group materials without tree-map allocations;
- intra-chunk neighbour queries use the dense voxel array directly;
- greedy slice scratch is fixed-size stack storage;
- HLMS shader debug dumping is disabled in Release.

## Large-world coordinate frame

The renderer is now floating-origin by construction. `FreeCameraController` stores a hierarchical `WorldPosition` (`GroupCoord int64 + local cell int32 + fraction float`). `Application` maintains a sticky integer render-group anchor. Ogre receives only camera coordinates relative to that anchor, and every chunk scene node is positioned through `chunkOriginRelativeToGroup()`. Crossing the anchor threshold rebases existing chunk nodes; no absolute world float enters Ogre.

## Debug HUD

`F5` toggles a top-left Ogre Overlay diagnostic HUD. It shows world/group-local
coordinates, current group origin, chunk/block coordinates, biome weights,
FPS/frame time, loaded chunk/group counts, streaming counters, look direction and
flashlight state. The text is refreshed at 5 Hz rather than rebuilt every frame.

The Overlay component requires HLMS Unlit for font rendering, so OgreRenderer now
registers both HLMS Unlit and HLMS PBS. Terrain remains PBS. See `docs/DEBUG_HUD.md`.

### File texture residency (v16.4)

Configured 2D PBS textures are created through OgreNext's `CommonTextureTypes`
file-texture presets rather than hand-assembling only the sRGB flag.  This is
important because Ogre's regular PBS texture path expects `AutomaticBatching`:
while a file is streaming Ogre may display a small dummy texture, and automatic
batching is what keeps HLMS datablocks informed when the real resident texture
replaces that dummy.  The loader also registers the concrete parent directory
of each resolved texture and loads by filename, avoiding ambiguity around
recursive resource archive names such as `textures/oak_leaves.png`.
