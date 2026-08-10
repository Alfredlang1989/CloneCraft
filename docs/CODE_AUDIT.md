# CODE AUDIT - prototype rendering repair

> **Historical audit note (updated 2026-08-10):** this file records the 2026-08-08
> repair. Current content/data ownership is summarized in
> `docs/DATA_DRIVEN_AUDIT.md`; later sections marked resolved are kept for history.


Date: 2026-08-08

This audit was made against the uploaded prototype. The working tree already
contained uncommitted milestone-06-style renderer/camera/mesh work before this
repair, therefore this repair deliberately does **not** create a Git commit or
tag that could accidentally claim unrelated user changes.

## Fixed in this repair

### 1. Greedy-mesh texture corruption

Root cause: a packed texture atlas was sampled with `TAM_WRAP`, while greedy
quads emitted UV ranges larger than one atlas tile. GPU wrapping repeats the
**whole atlas**, not one sub-rectangle, so one stone quad walked through stone,
dirt, grass, sand, water, etc.

Fix:

- removed the active `BlockAtlas` rendering path;
- greedy UVs are now local block-texture UVs (`0..merged width/height`);
- one block type uses one Ogre material/texture section;
- `TAM_WRAP` therefore repeats only the selected block texture.

This is intentionally the simple/prototype-safe solution. A future optimisation
may batch block textures into a real texture array/bindless scheme without
changing the CPU mesh semantics.

### 2. Block colours were hardcoded in C++

The old `BlockAtlas.cpp` contained an explicit C++ RGB table for
`core:stone`, `core:dirt`, `core:grass`, `core:sand`, `core:water` and
`core:gravel`.

Fix: visuals now live in `data/blocks.json`.

Visual resolution order is exactly:

1. `texture` when configured and loadable;
2. otherwise `color`;
3. otherwise a deliberately loud built-in magenta/black test texture.

`color` accepts `#RRGGBB`, `#RRGGBBAA`, `[r,g,b]` or `[r,g,b,a]`.

### 3. SDL keyboard-state out-of-bounds access

The old input state array had only 128 entries. SDL3 scancodes span
`SDL_SCANCODE_COUNT` (512), so keys such as left shift could read outside the
array.

Fix: key state uses `std::array<unsigned char, SDL_SCANCODE_COUNT>` and bounds
checks. OS key-repeat no longer emits fake key-down edge callbacks. Mouse
relative motion keeps SDL's float precision instead of truncating to integer.

### 4. Runtime BlockId air invariant

The code assumed `core:air` happened to be the first JSON block even though
runtime id 0 is semantically AIR.

Fix: `BlockIdTable` explicitly reserves runtime id 0 for `core:air`, regardless
of JSON order.

### 5. Chunk size was partially hardcoded to 16

Greedy-mesher slice arrays and parts of worldgen assumed 16 even though the
architecture explicitly wants 16/32 experimentation.

Fix:

- CMake cache: `CLONECRAFT_CHUNK_EDGE` and `CLONECRAFT_GROUP_EDGE`;
- central constants in `Coords.h`;
- greedy temporary grids are dynamically sized from the configured edge;
- coordinate tests were verified with both 16/16 and 32/32.

### 6. Large-coordinate worldgen hash truncated coordinates

The old hash cast X/Z to 32 bit, making generation repeat every `2^32` blocks.

Fix: all 64 coordinate bits are mixed into the deterministic hash.

### 7. Chunk streaming was actually still 2D

Despite cubic `ChunkCoord`, streaming requested only `(x, y=0, z)` and worldgen
returned a complete vertical column. This made `chunk.y` largely decorative.

Fix:

- streaming now requests a cubic `(2r+1)^3` active region;
- worldgen generates exactly one requested cubic chunk;
- negative Y chunks are ordinary world space, not an artificial floor;
- all-AIR chunks are still materialized while in range so `loaded AIR` and
  `unloaded` remain distinct states;
- eviction checks X, Y and Z.

### 8. Loaded AIR was conflated with unloaded world

`ChunkManager::blockAt()` returned 0 for both states.

Fix: `tryBlockAt()` returns `nullopt` for an unloaded chunk and a numeric value
(including AIR=0) for a loaded one. The old convenience `blockAt()` remains for
callers that intentionally want unloaded-as-air semantics.

### 9. Cross-chunk visibility invalidation was incomplete

Loading/unloading a chunk, or changing a voxel on a chunk boundary, changes the
faces of neighbouring chunks too.

Fix: chunk load/unload invalidates all six neighbour chunk coordinates; a
boundary voxel edit invalidates only the relevant neighbour(s).

### 10. Resource generation could emit duplicate deltas for one coordinate

Base stone and a resource replacement could both be emitted at the same block,
then sorted with no stable ordering guarantee for equal positions.

Fix: resource selection is computed per column and replaces stone during voxel
generation, producing at most one generated block value per position.

### 11. Biome surface selection was per-column hash noise

`biomeAt()` previously made an independent weighted pseudo-random pick for each
X/Z column. That creates visible biome salt-and-pepper noise and does not match
the planned continuous biome field.

Fix: low-frequency OpenSimplex climate fields (temperature/rainfall) now produce
continuous normalized biome weights. `biomeAt()` returns the dominant weight for
the still-discrete surface block choice.

Resolved in v18.3.5: the shared surface is now blended from each biome's
`terrainMaskField` and data-owned terrain profile. The v18.3.9 AST/data audit also
moved the remaining detail-noise scale/amplitude literals into biome data.

### 12. Sticky group rebase failed for very large one-frame jumps

The previous correction loop only handled a small fixed number of group
crossings.

Fix: the number of group steps is calculated directly, so teleports/lag jumps
across hundreds or thousands of groups rebase in one update.

### 13. Renderer material/texture lifetime

Per-block HLMS datablocks are now destroyed after renderables and before owned
generated textures. File textures retrieved from Ogre's resource manager are
not treated as uniquely owned by this renderer.

## Remaining architectural work / known issues

### A. Biome terrain parameters (resolved in v18.3.5 / v18.3.9 audit)

`surfaceHeight()` now uses the same biome-adjusted surface as chunk generation.
`biomes.json` owns offsets, base/detail scaling, ridge and island shaping, while
`terrainMaskField` binds profiles to continuous Lua masks. The later AST audit moved
`detailScale` and `detailAmplitude` out of `WorldGen.cpp` as well.

### B. Huge-coordinate render precision (fixed in v10.2)

The old path placed chunk scene nodes at absolute float chunk coordinates and passed a global camera double through a float cast. It has been replaced by `WorldPosition` plus a sticky int64 render-group anchor. `OgreRenderer::setCameraLocalPose()` accepts local coordinates only; `ChunkWorldRenderer` rebases all chunk nodes to the same integer anchor. The old global camera XYZ accessors no longer exist.

### C. Physics/Jolt is not integrated yet

No issue for the current renderer prototype, but the architectural invariant
remains: ChunkGroup != Jolt PhysicsSystem. Physics needs one local
`PhysicsContext` with an anchor so bodies from adjacent logical groups interact.

### D. True fixed timestep is not implemented yet

`Application::runFrameUpdate()` currently computes a clamped variable delta and
uses it for the free camera. That function name is ahead of reality. A real
accumulator/fixed simulation step is still required before Jolt/gameplay.

### E. Texture implementation is intentionally draw-call heavy

The safe prototype solution groups each chunk into one ManualObject section per
block type. This fixes correctness first. With many block types, a future
texture-array/bindless renderer should reduce material sections/draw calls.

### F. Transparent materials are basic only

`transparent=true` now enables alpha blending and disables depth writes, and
colour alpha is supported. Proper water/glass ordering, refraction, depth
pre-pass, separate transparent queue policy, etc. are future renderer work.

### G. Full build was not possible in this execution container

No operating-system packages were installed or changed. This container lacks
the SDL3/OgreNext development setup, so the graphical application cannot be
configured here. nlohmann/json is vendored in-tree and is no longer a host
dependency.

Renderer-independent syntax checks were run for the changed coordinate, chunk,
meshing and worldgen sources. Coordinate tests pass for both default 16/16 and
experimental 32/32 compile-time sizes.


### 14. Outer streaming-rim faces could disappear

An unloaded neighbour and loaded AIR are deliberately different world states,
but hiding a face merely because the adjacent chunk is not loaded creates
visible holes around the active streaming volume.

Fix: meshing renders an unloaded-neighbour face provisionally. Chunk load/unload
invalidates the six adjacent chunks, so once the neighbour is known the face is
kept for AIR or culled for solid terrain.

### 15. Unknown BlockId silently became AIR

The previous `BlockIdTable::indexOf()` returned runtime id 0 for an unknown
string id. Since id 0 is AIR, a typo or missing mod block could silently erase
world content.

Fix: `indexOf()` and out-of-range `idOf()` fail with `RegistryError`;
`tryIndexOf()` exists for explicit optional probing.

### 16. Hidden terrain floor contradicted the unbounded-Y model

`surfaceHeight()` used to clamp generated surface height to at least 1. That was
an implicit Y-floor hidden inside worldgen.

Fix: negative surface heights are valid and covered by a worldgen test.

### 17. Cave subtraction only affected deep stone

The earlier generator checked cave noise only in the deep-stone branch, so a
cave could not cut shallow filler/surface and the effective pipeline did not
match the intended terrain -> resources -> cave subtraction model.

Fix: choose the terrain/material/resource result first and apply cave noise
afterwards to all solid terrain layers.

### 18. Registry optional/default validation holes

A biome without `fillerBlock` produced an empty id that could fail later inside
worldgen; climate values were not range-checked; optional biome `resourceId`
was not cross-validated after resources loaded.

Fix: omitted filler defaults to the surface block, temperature/rainfall are
validated to 0..1, and non-empty resource references must resolve.

### 19. Duplicate SDL event ownership / sticky keys / repeated shutdown

The platform bridge still carried a second event-polling path even though
`InputManager` is the intended single consumer. Focus loss could also leave
movement keys pressed, and explicit platform shutdown plus destructor shutdown
could call the SDL teardown path twice.

Fix: the dead duplicate event pump was removed, focus loss clears key state,
and `PlatformWindowBridge::shutdown()` is idempotent.

### 20. Renderer callback lifetime

`ChunkManager` stores a change callback that captures `ChunkWorldRenderer`.
Leaving it registered after renderer destruction would be a dangling callback.

Fix: the renderer detaches the callback during shutdown while the Application-
owned ChunkManager is still alive.

### 21. OgreNext 4.x unlit texture binding API

The repaired renderer originally used a pointer-style `setTexture()` call plus
`setSamplerblock()`. Current OgreNext 4.x `HlmsUnlitDatablock` instead binds a
texture by resource alias and accepts the `HlmsSamplerblock` directly in
`setTexture()`.

Fix: block materials now call `setTexture(0, texture->getNameStr(), &sampler)`.
The sampler uses `TAM_WRAP`, so greedy UVs repeat only the one texture attached
to that block material.

## Follow-up build repair after real Ubuntu 24.04 / GCC 13 build

The first real build on the target machine exposed a compile-time bug that the
previous harness did not reproduce because its standard-library compile path had
not instantiated `std::map<BlockCoord,...>::operator==`.

### Coordinate value equality was missing

`BlockCoord` (and `GroupCoord`) had ordering for use as `std::map` keys but no
value equality. GCC 13 correctly rejected `std::map<BlockCoord,std::string>`
equality in `TestWorldGen.cpp` because `std::pair::operator==` must compare the
keys too.

Fix:

- `BlockCoord`, `ChunkCoord`, and `GroupCoord` now all define C++20 defaulted
  `operator==`;
- `TestCoordinates` contains a regression test that instantiates and compares
  two `std::map<BlockCoord,std::string>` values, so this exact compile failure
  cannot silently return.

### Greedy UV test expected the wrong number of quads

The full chunk-width, one-block-thick slab test expected two quads, but the
renderer intentionally emits provisional faces at unloaded streaming borders.
Such a slab has six greedy surface quads: top, bottom, and four sides. The test
now expects six while still verifying that the top UV range spans exactly one
texture repetition per block.

### SDL3/OgreNext X11 bridge tightened

The target is OgreNext 4.0.0unstable on X11. The GLX `parentWindowHandle` is now
passed in Ogre's documented `display*:screen:windowHandle` form instead of only
an XID. Ogre Root creation also uses `generateAbiCookie()`, as recommended by
OgreNext 3.x/4.x, and resize forwards both `requestResolution()` and
`windowMovedOrResized()`.

SDL still owns the native application window; no SDL renderer is created.


## Performance follow-up: behaviour-preserving hot-path repair

The renderer-correctness v2 was profiled with an identical before/after core
benchmark. Several hot paths were doing avoidable allocation/hash/tree work.

### 22. Streaming performed a string round-trip per generated voxel

The old path generated `BlockDelta{BlockCoord,std::string}`, then looked each
string up again in `BlockIdTable` while filling the chunk. WorldGen now exposes
`generateChunkIds()` for runtime streaming, which writes compact `uint16_t` ids
directly into a reusable dense buffer. The stable string API remains for
persistence/tests.

### 23. Worldgen repeatedly allocated biome vectors and rescanned resources

Dominant-biome selection in the hot generation loop now evaluates the same
scores directly without constructing `BiomeWeight` strings/vectors per column.
Biome surface/filler ids, resource block ids, and total resource weight are
resolved once per generated chunk. The public normalized biome-vector API is
unchanged.

### 24. Delta generation sorted data that can be emitted in order

The compatibility string API now traverses the compact chunk result directly
in x/y/z order. The previous `std::sort` pass is gone.

### 25. Empty 3D chunks paid full meshing cost

`Chunk` now caches its non-air count. Streaming receives the count directly
from WorldGen, and `ChunkMeshBuilder` returns immediately for all-AIR chunks.
This matters strongly in a cubic active volume because many chunks above terrain
are legitimately empty.

### 26. Meshing performed hierarchical lookup for local neighbours

For EDGE=16, 93.75% of one-axis neighbour tests remain inside the same chunk.
Those now read directly from the dense chunk buffer. Only boundary checks use
`ChunkManager`/ChunkGroup lookup. Greedy slice scratch also uses fixed-size
arrays instead of heap allocations, and opaque flags are cached per builder.

### 27. Renderer did work on unchanged frames

`ChunkWorldRenderer::sync()` now returns immediately when its dirty set is empty.
During actual rebuilds, dense runtime ids are used to group material sections
instead of a tree map. Release builds also stop HLMS shader-debug output.

### 28. Prototype sky was unnecessarily dark

The basic compositor clear colour is now `#78A7FF` (RGB 120,167,255), a bright
Minecraft-like daylight sky. This is a prototype clear colour, not a final
atmosphere implementation.

### Performance validation

Median same-harness A/B timings: worldgen 109.657 -> 59.786 ms for 256 chunks,
radius-2 streaming 54.280 -> 25.615 ms, mesh-all 45.893 -> 17.365 ms.
Generated world data was hash-identical across 648 chunks, and generated mesh
data was hash-identical across the full radius-2 loaded region. See
`docs/PERFORMANCE.md`.

## PBS / sanitizer follow-up (2026-08-08)

### 26. Flat Unlit terrain could not express requested material properties

Terrain used HLMS Unlit, so roughness, Fresnel/specular reflection, sun lighting,
normal maps and received shadows were structurally unavailable.

Fix: terrain materials now use HLMS PBS. Block PBR parameters are parsed from
JSON and translated once when the per-block datablock is created. Greedy meshes
now carry tangents so tangent-space normal maps are usable.

### 27. Shadow-cast policy cannot be per ManualObject section

Ogre shadow casting is a MovableObject-level property, while a chunk ManualObject
contains multiple material sections. A single object therefore cannot faithfully
represent JSON `castShadows=false` for water while stone in the same chunk casts.

Fix: a chunk is split into at most two ManualObjects: shadow-casting and
non-shadow-casting. This preserves material batching without creating one object
per block type. A dense runtime-ID shadow-policy array avoids registry/hash lookups
in the hot rebuild path.

### 28. OpenSimplex2 C++ port used undefined signed overflow

UBSan found the vendored LCG multiplying signed `long` values past their range.
The algorithm intends Java-style two's-complement wraparound.

Fix: the LCG now operates in `uint64_t` (defined modulo 2^64) and bit-casts to
`int64_t` only where Java signed remainder semantics are required. A comparison
against the previous implementation built with `-fwrapv` produced the exact same
noise hash (`f9257277419ea1d6`) for a multi-seed 2D/3D sample set.

### 29. OpenSimplex2 static lookup initialization leaked heap nodes

ASan/LSan found the third-party 3D lattice lookup allocating linked nodes with
`new` and never freeing them. This was process-lifetime lookup data, not dynamic
runtime state.

Fix: the same pointer topology now lives in function-static fixed storage. No
heap allocation is needed and ASan/LSan reports clean shutdown.

### Validation

Renderer-independent builds/tests pass for EDGE/GROUP 16/16 and 32/32. A strict
GCC14 build with `-Werror -Wpedantic` passes all seven suites. ASan + UBSan + leak
detection also passes all seven suites after the OpenSimplex repairs.


## Runtime crash fix after PBS/shadows integration

Target log showed two renderer defects after all CPU tests had passed:

1. Generated JSON-colour textures were bound by resource *name*. Ogre then asked
   ResourceGroupManager for a file named `Clonecraft/Generated/...`, producing
   FileNotFoundException messages. Generated textures are now `ManualTexture` and
   PBS binds the existing `TextureGpu*` directly.
2. PBS HLMS was registered with a hand-written, incomplete library list. OgreNext
   4.x default PBS paths also include `Pbs/Any/Main` and `Pbs/Any/Atmosphere`. Missing
   shader pieces produced invalid generated GLSL (`unexpected '}'`). Registration now
   uses `Ogre::HlmsPbs::getDefaultPaths()` so it follows the installed OgreNext build.

Tangents are now sent to Ogre only for sections whose block material actually has a
normal map. This avoids unnecessary vertex input/bandwidth for ordinary voxel terrain.

## Runtime fix v6: OgreNext HLMS Media root

OgreNext `HlmsPbs::getDefaultPaths()` returns shader paths relative to Ogre's Media root
(e.g. `Hlms/Pbs/GLSL`). `OGRE_NEXT_HLMS_DIR` points to `.../Media/Hlms`, so appending the
returned path directly produced `.../Media/Hlms/Hlms/Pbs/GLSL` and failed at runtime.
The renderer now steps up to `.../Media/` before appending Ogre-provided paths.
HLMS setup exceptions are also caught during renderer initialization instead of escaping and
causing an abort/core dump.
