# Code review v10: architecture, dead code, data ownership and performance

> **Historical review note (updated 2026-08-10):** this file records the v10
> review and therefore contains historical findings. Current worldgen/settings
> ownership is summarized in `docs/DATA_DRIVEN_AUDIT.md`. Biome terrain blending,
> async chunk generation, rivers, decorations and persistent settings have moved on
> since the original review.


Date: 2026-08-08

This review was performed against the v9.1 debug-HUD source snapshot. It focuses
on source/module structure, hot paths, data ownership, static analysis readiness,
dead code and the remaining architectural gaps. Renderer-specific code could be
read and API-audited here, but the real OgreNext 4.0 GL3+ executable still has to
be compiled/run on the target Ubuntu machine.

## What was changed during this review

### Mesh data and hot-path cleanup

- Renamed the misleading duplicate `src/world/mesh/Chunk.h` to
  `ChunkMesh.h`. The real voxel `Chunk.h` remains under `world/chunk`.
- Removed `ChunkMesh::indices`. Runtime rendering never consumed this vector;
  Ogre rebuilt the same two triangles from each four-vertex greedy quad.
- Removed the dead non-greedy meshing branch and its always-true
  `kGreedyEnabled` switch.
- Tangent math is now performed only for block types that actually declare a
  normal map.
- `ChunkWorldRenderer` now owns one persistent `ChunkMeshBuilder` and one mesh
  scratch buffer instead of rebuilding both for every dirty chunk.
- Streaming evicts chunks outside the new radius before generating the incoming
  slab, reducing the temporary memory peak during chunk crossings.
- Removed duplicated chunk-group count state; `ChunkGroup::chunkCount()` is
  derived from its sparse container.

### Dead or misleading APIs

- Removed unused `InputManager::requestShutdown()` state. Shutdown remains the
  explicit SDL event callback into `Application`.
- Removed unused runtime log-level setter/state.
- Removed unused `ChunkGroup::capacity()` and `ChunkManager::forEachGroup()`.
- Renamed `Application::runFixedStep()` to `runFrameUpdate()`. It is a
  variable-rate frame update and must not pretend that a fixed simulation tick
  already exists.

### Debug/UI boundary

- Moved HUD string formatting into renderer-independent `src/debug/`.
  `Application` gathers facts; the debug module formats them; Ogre's
  `DebugOverlay` only presents text.
- HUD voxel lookup now uses `tryBlockAt()`, so unloaded space is displayed as
  `<unloaded>` instead of being silently presented as `core:air`.

### Worldgen data ownership

New `data/worldgen.json` owns the base generator settings, including:

- seed;
- height scale/amplitude/base height;
- biome scale/sharpness;
- sea level;
- cave scale/threshold;
- base terrain block id;
- fluid block id;
- surface depth.

The previous runtime literals `core:stone`, `core:water` and filler depth 3 are
therefore no longer embedded in the generator.

Resource definitions gain explicit `chance` in addition to relative `weight`.
A biome's existing `resourceId` field is now actually honored by generation;
previously it was parsed/validated but ignored. An explicit biome resource works
even if its global weighted-pool weight is zero. `chance=0` is guaranteed to
place nothing (`roll < chance`, not `<=`).

## Static architecture result

The new architecture gate currently passes. At review time the project include
graph was:

```text
app -> camera, core, debug, input, platform, render, world.chunk,
       world.registry, world.worldgen
camera -> none
core -> none
debug -> none
input -> core
main -> app, core
platform -> core
render -> core, platform, world.chunk, world.coordinates, world.mesh,
          world.registry
world.chunk -> world.coordinates, world.registry, world.worldgen
world.coordinates -> none
world.mesh -> world.chunk, world.coordinates, world.registry
world.registry -> none
world.worldgen -> world.coordinates, world.registry
```

There is no forbidden module cycle and no duplicate header basename after the
`ChunkMesh.h` rename. The gate also blocks external framework leakage, for
example Ogre/SDL headers appearing in renderer-independent world/core modules.

The checker reports two large translation units without failing the build:

- `src/render/ChunkWorldRenderer.cpp`: about 730 lines;
- `src/render/OgreRenderer.cpp`: about 550 lines.

They are the clearest modularity debt left in the current source.

## Modularity review

### Clean boundaries

The following are already in good shape:

- coordinate math is independent from renderer/input;
- registry types are renderer-agnostic data structures;
- worldgen does not include Ogre or SDL;
- chunk storage/streaming does not know about Ogre;
- greedy meshing produces renderer-neutral CPU geometry;
- SDL native handles stay inside the platform/render boundary;
- debug text formatting does not depend on Ogre;
- `Application` remains the lifetime/orchestration root rather than a base class
  inherited throughout the engine.

### Boundaries that should be split later

`ChunkWorldRenderer.cpp` currently combines three separate responsibilities:

1. translating `BlockDef` into PBS materials/textures;
2. converting a `ChunkMesh` to Ogre `ManualObject` sections;
3. tracking dirty chunks and Ogre scene-node lifetime.

A later low-risk split is naturally:

```text
render/material/BlockMaterialCache
render/mesh/OgreChunkMeshUploader
render/ChunkWorldRenderer
```

`OgreRenderer.cpp` likewise combines Ogre bootstrap/HLMS registration, external
window setup, compositor/shadow configuration, camera, sun/ambient and
flashlight. Natural later components are `OgreBootstrap` and `LightingSystem`.
This split should happen only with a clean target build because these paths are
Ogre-version sensitive.

## Data-driven review

### Already data-driven

- block mechanics and PBR material fields;
- texture vs color fallback;
- biome climate targets and surface/filler blocks;
- resource block/weight/chance/height range;
- base worldgen scalar values and base/fluid block ids.

### Still hardcoded and good candidates for JSON

Application/client settings status:

- window width/height/fullscreen/resizable, mouse sensitivity, free-camera speed,
  chunk render distance and commit budget are now persisted in `settings.json`;
- camera start pose and HUD cadence/default visibility remain code-owned.

Render settings status:

- RenderSystem/plugin/options, shadow far distance, Forward3D grid/light budget and
  camera near/far clip are now persisted in `settings.json`;
- sky/background, ambient, sun, flashlight and detailed PSSM/shadow-map tuning remain
  renderer constants and are the current data-ownership candidates.

Block schema extensions that fit the existing material model:

- optional emissive color/intensity instead of grayscale `emission` only;
- alpha-cutout mode/threshold for leaves/fences rather than treating every
  transmitting material as generic blended transparency;
- optional texture sampler policy if non-block materials later need clamp vs
  repeat.

These are review findings, not silently implemented renderer changes in this
revision. Keeping the Ogre-sensitive patch small is more valuable than moving
all constants at once without a target GPU build.

## Bugs / risks found or clarified

### 1. Worldgen biome weights did not shape terrain height (resolved)

Resolved in v18.3.5: `surfaceHeight()` and chunk generation share the same
biome-profile blend. See `docs/DATA_DRIVEN_AUDIT.md` for the current boundary.

### 2. Resource generation is still a placeholder algorithm

It now honors its JSON semantics correctly, but it still attempts at most one
single-voxel resource per X/Z column. There are no 3D veins, thresholds,
replacement masks or per-resource noise fields yet.

### 3. Huge-coordinate rendering was a real precision bug and is now repaired

The previous camera stored absolute doubles and cast them to Ogre floats; chunk scene nodes also used absolute float chunk origins. That violated the huge-world design. Runtime positions now use `WorldPosition` (`GroupCoord int64 + local int32 cell + float fraction`), while a sticky integer render anchor keeps Ogre coordinates local. Chunk nodes and the camera are always expressed in the same local frame. Regression tests exercise coordinates around 30 quadrillion groups from origin.

### 4. Streaming/worldgen/meshing are synchronous

Entering a new chunk can generate and mesh a slab of chunks on the main thread.
The next scaling bottleneck is a bounded job pipeline with generation/meshing on
workers and GPU/Ogre upload on the main thread with a frame budget.

### 5. No real fixed simulation timestep yet

Camera motion currently uses clamped variable frame delta. Jolt/gameplay must
not be integrated until an accumulator/fixed-tick layer exists.

### 6. Material/draw-call scaling

Correctness currently wins: each block material becomes a ManualObject section.
With many mod block types this can grow draw calls. A texture-array/material
index path is a later optimization, but it must not reintroduce the original
packed-atlas wrap bug.

### 7. Texture fallback is filesystem-safe, not fully decode-safe

The renderer checks whether a configured texture file exists before asking Ogre
to stream it. Because Ogre texture residency/loading can be asynchronous, a
corrupt/unsupported file may fail after a `TextureGpu*` has already been
returned, so the JSON color fallback is not yet a guaranteed late-load fallback.
A future material cache should own texture load state and substitute the color
when the real resource reports failure.

### 8. Resource names are not formally constrained

Documentation says ids are namespaced (`namespace:name`), but the loader does
not yet enforce a canonical id grammar. The renderer sanitizes ids for Ogre
resource names; two different unusual ids could theoretically sanitize to the
same name. Canonical id validation should be added before third-party mods are
accepted as untrusted content.

### 9. Platform coverage

The current external-window bridge is proven around X11. Native Wayland support
is still an explicit platform task, not something gameplay/world code should
know about.

### 10. Real refraction is not complete

The block JSON stores refraction/IOR and PBS gets the parameters, but true
screen-space refraction still requires the dedicated compositor/refraction
pass.

## Static analysis policy from this revision onward

- `clang-tidy` is the established AST/semantic analyzer.
- `.clang-tidy` is committed with analyzer, bugprone, performance and
  portability checks.
- CMake always emits `compile_commands.json`.
- `compile.sh` runs architecture validation, configures CMake, runs clang-tidy,
  then builds and tests.
- `tools/architecture_check.py` independently enforces dependency direction,
  module cycles, project include cycles and duplicate header basenames.
- `third_party/` is excluded from clang-tidy ownership.
- local agents never install clang-tidy or other OS packages. Missing tooling is
  reported as a blocker.

See `docs/STATIC_ANALYSIS.md` for commands and policy.


## v11 worldgen follow-up (2026-08-09)

The v10 finding that biome weights did not shape terrain has been resolved.
Terrain/climate/cave/river responsibilities were split into dedicated
renderer-independent systems. Biomes now own height/detail/ridge/island terrain
profiles; continentalness enables broad ocean/land regions; rivers and surface
material repair are explicit stages; caves use connected spaghetti-style
implicit-surface intersections.

The remaining worldgen architecture debt is no longer terrain-biome coupling.
It is primarily resource morphology (still one single-voxel attempt per X/Z
column) and synchronous generation/meshing on the main thread. The richer cave
field is also the largest added CPU cost and should be a priority input to the
future bounded worker-job pipeline.
