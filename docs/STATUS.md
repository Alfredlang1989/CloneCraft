# STATUS

## Primus impetus status (2026-08-13)

- M00 — Agent-/Dokumentations-Baseline: **done** (docs-only commit).
  `INDEX.plan` is on the OmniGrid/agent workflow (read-first order, Graphify
  rules, AST-vs-Graphify split, end-of-run gate); `docs/MILESTONES.md` is
  synced with the Primus-impetus roadmap; this file describes the current state.
- M01 — Rename (#14): **in progress, uncommitted**. A CloneCraft -> OmniGrid
  branding sweep sits in the working tree (docs, build names, env var names,
  source comments) but has not been committed. It is foreign working-tree work
  and is not part of the M00 commit. Persistent technical ids (block ids,
  mod namespaces, save/protocol identities) remain untouched.
- M02 — Worldgen baseline (#8): **next**. Broaden desert climate distribution,
  define `BiomeDef::weight` semantics, add determinism/statistics tests for
  seed 1337 plus at least one additional seed.
- Core spine #3 (contracts/sidecars/world state/actions/events/enTT/RocksDB),
  #18 (player interaction), #7 (Lua callback cache), #13 (client/server),
  #16/#17 (construction): **not started**.
- Baseline gates at M00 time: architecture check PASS, clang-tidy/AST PASS,
  15/15 renderer-independent ctest suites PASS.

## Current state

Prototype has progressed beyond milestone 05 into voxel rendering/camera work.
A rendering/correctness repair was applied on 2026-08-08 to the uploaded working
tree. The tree already contained uncommitted user/agent work, so this repair was
**not committed or tagged** to avoid mixing unrelated changes.

## v18.3.9 — AST/data-driven/documentation audit (2026-08-10)

- audited the headless content/config boundary with filtered Clang 17 JSON AST dumps
  for WorldGen, WorldGenConfigLoader, RegistryLoader, Settings and UiConfig;
- confirmed production WorldGen contains no concrete material ids or river/biome field
  names; river/sediment/cave-sealing semantics remain JSON/Lua-owned;
- found and removed one small biome-terrain data leak: detail noise scale `0.018` and
  amplitude `3.0` moved from WorldGen C++ into `biomes.json` as `detailScale` and
  `detailAmplitude`, preserving the shipped terrain numerically;
- reconciled INDEX/current docs with active biome terrain, rivers, cave sealing,
  decorations, bundled nlohmann/json and persistent settings;
- documented the remaining renderer-side data-ownership debt (sky/ambient/sun/
  flashlight/PSSM tuning).

## River replace-only + one-sided mountain tunnels (2026-08-10)

- fixed a v18.3.5 desynchronisation: `surface_height` had moved to the new low,
  biome-adjusted terrain profile while `river_valley_depths.lua` and the tunnel
  route still reconstructed the old +38/massif height formula;
- removed the normal-river Air valley passes and their obsolete cut-depth fields;
  an ordinary river now only replaces existing terrain with water/bed/bank
  material and therefore cannot become a floating water road across pre-existing
  Air;
- removed `river_tunnel_floor_space`; mountain tunnel carving is limited to the
  chamber above the Y=0 river, with no river-generated Air below the waterline;
- tunnel activation now follows the shared continental/rugged/crown massif
  signal used by the high-mountain masks, so the river route cannot drift away
  from the biome-height system again.

## Phase 1: hierarchical addresses + mapped v16 OpenSimplex (2026-08-10)

This branch was rebuilt from v16.5 rather than continuing the v17 terrain-noise
experiment.

- production spatial identity is `Sector -> Region -> Group -> Chunk -> Block`,
  with every discrete component stored as int64 and no global Block/Chunk XYZ;
- Chunk/ChunkGroup physical radices remain 16; Region/Sector logical radices are
  isolated so Phase 2 can enlarge them without changing storage layout;
- v16 OpenSimplex2S is retained. A ChunkGroup mapper folds the hierarchical
  origin into OpenSimplex's transformed-lattice phase and samples normal voxels
  from local 0..255 coordinates;
- Lua no longer receives world XYZ. Noise helpers take local scale + salt and
  optional small domain-warp offsets;
- a 32-Group low-pass hierarchy-hashed X/Z warp (64-block amplitude) slowly
  bends the v16 noise so its finite permutation does not become a simple repeat;
- the shipped worldgen remains the v16 graph: 24 fields, 29 passes, v16 river
  thresholds and v16 cave formulas;
- 3D fields are split into 16 X-slices for worker-pool utilisation.

Validation summary: mapped main noise with macro warp disabled differs from v16
by at most 1.4e-12 (2D) / 2.5e-12 (3D) in the current corpus. Across eight
spread terrain windows, mean local slope differs by ~0.05% and river threshold
coverage remains within a few percent relative. A dispersed 1,048,576-voxel
cave sample gives 9.08% carved in v16 versus 9.17% in Phase 1. Serial all-field
sampling is ~12.4% slower than v16; with 2-5 workers the X-sliced field phase is
roughly parity to modestly faster in this noisy shared-CPU harness. See
`PHASE1_VALIDATION.md`.

Architecture, coordinate, chunk, streaming, mapped-noise, focused WorldGen and
default river/cave morphology tests pass. The mapped-noise, focused WorldGen hierarchy/determinism and river/cave
morphology suites pass ASan/UBSan, and touched core files pass strict warning
compilation. The same coordinate/noise tests also pass when the
logical Region and Sector radices are compiled at 9e18; those radices are not
enabled in the shipped Phase-1 build.

## High-mountain river tunnels + desert mountains (2026-08-09)

- fixed the extreme-mountain river case: an open valley is used below original terrain Y=240, while higher terrain switches to a buried tunnel around the fixed Y=0 river instead of excavating a hundreds-of-block canyon;
- tunnel clearance grows continuously from 10 to 50 blocks and its horizontal chamber width also grows with massif height;
- the tunnel is built from nested 2D masks/ceiling fields and existing generic `surface_layer` passes, so production WorldGen C++ remains unchanged and no extra per-voxel 3D Lua field was added;
- normal hills, plains, deserts and modest mountains keep the existing V-valley river treatment;
- added active `Desert High Mountains`: hot/dry massif cores get a two-block sand cap, fourteen sandstone blocks and ordinary stone/geology below, overriding cold alpine/snow surface passes by priority;
- the descriptive biome registry now also contains `core:desert_high_mountains`.

Regression probes for seed 1337: a river center at `(27648,-26048)` crosses original terrain Y=705, keeps water at Y=-3..0 and gravel at Y=-4, guarantees Air Y=1..50, and has solid mountain again above the tunnel instead of an open 705-block canyon. A stable desert-high-mountain column at `(6584,-15048)` is `sand x2 -> sandstone x14 -> stone`.

## Terrain variety + fixed-level river valleys (2026-08-09)

- active terrain now includes Plains, Temperate Forest, Rolling Hills, Desert, Badlands, Alpine Highlands and High Mountains;
- the active height Lua field creates broad mountain massifs with sampled peaks above Y=700;
- new data-driven surface blocks include forest grass, snow, red sand and red sandstone;
- climate masks share the same field seed basis as `surface_height`, so mountain/snow/alpine masks align with the terrain they describe;
- cave frequency/scale was widened to produce thick walkable spaghetti tunnels; the regression data contains a stable 3x3x3 clear underground volume;
- river water is fixed at world Y=0, gravel bed at Y=-4; five nested 2D valley cuts widen upward through obstructing terrain, so mountains are cut into valleys instead of making water climb;
- river valleys deliberately stay 2D-field-driven, avoiding an extra 4096 Lua calls per non-air chunk;
- no production C++ worldgen code was required for this terrain change: it is JSON/Lua/block-data only.

Validation in this harness: JSON/data-graph checks pass, the architecture gate passes, the edited regression test translation unit is warning-clean under `-Werror` with a declaration stub for the unavailable JSON header, and a manual full WorldGen probe passes ASan/UBSan. A sampled river through original terrain Y=139 has water only at Y=-3..0 and is clear to the old surface; the same cross-section widens from 78/257 air samples near the floor to 243/257 near Y=130.

## Flight streaming + rivers/deserts follow-up (2026-08-09)

- chunk generation moved off the frame thread; radius-3 chunk crossings no longer synchronously generate a 49-chunk slab;
- ready chunks are committed in a bounded per-frame budget and mesh/Ogre rebuilds are budgeted separately;
- Lua states/noise caches and WorldGen worker threads are persistent across chunks;
- all-Air sky chunks skip 3D cave/geology field evaluation;
- replacement tags compile to dense runtime-id masks and merge order is cached;
- active biome masks now cover forest, desert, badlands, alpine and high-mountain regions; high-mountain massifs can exceed 600 blocks and may receive snow caps, while deserts keep `sand -> sandstone -> stone` strata;
- rivers now use a warped zero-contour at fixed world Y=0; five cheap 2D valley cuts widen upward through hills/mountains, with sandy banks, water at Y=-3..0 and gravel at Y=-4;
- debug HUD reports queued/ready streaming chunks.

Measured harness result: the radius-3 incoming 49-chunk slab fell from ~171.7 ms CPU generation in v12 to ~56.6 ms despite the added river/desert fields. The real-time `ChunkStreamingManager::update()` crossing call is ~0.06 ms because generation is asynchronous. ASan/UBSan and worker-count determinism probes pass.

## Data-driven worldgen runtime (2026-08-09)

- active chunk material generation no longer calls `TerrainSystem`, `RiverSystem` or `CaveSystem`;
- `data/worldgen.json` defines generic fields and passes instead of fixed terrain/material settings;
- Lua 5.4 scripts own 2D/3D scalar fields and receive deterministic native `noise2`/`noise3` helpers;
- default fields now include surface height, variable dirt depth, deep dirt pockets, gravel pockets and spaghetti caves;
- generic pass types are `fill_below`, `surface_layer`, `surface` and `volume`;
- every pass emits an independent `BlockProposal` array; fields and passes are sampled/built in parallel;
- merge order is deterministic by priority then JSON declaration order, independent of worker scheduling;
- block registry entries now support semantic tags used by `replaceTags`;
- cave subtraction is represented as a high-priority `core:air` volume pass;
- `ChunkStreamingManager` no longer carries biome/resource/table state into worldgen;
- worldgen Lua removes OS/filesystem/module APIs and the stock nondeterministic PRNG;
- beaches, vegetation and structures remain later post-process stages; rivers and desert strata are now expressed as generic masked passes.

Validation in this harness: the new hot path compiles with `-Wall -Wextra -Wpedantic -Werror`; ASan/UBSan probes pass; worker-count determinism was checked across 150 chunks (1 vs 8 workers); default-data sampling produced stone, dirt, grass and gravel plus cave/air subtraction; coordinate and standalone river/cave regression suites remain green; architecture check passes. Full JSON-loader CMake build still requires the project dependency `nlohmann/json.hpp`, which is not installed in this harness.

## Worldgen v11 follow-up (2026-08-09)

- split generation into modular `TerrainSystem`, `RiverSystem` and `CaveSystem`;
- biome weights now include low-frequency continentalness and biome-specific terrain profiles;
- added `core:ocean` with broad below-sea basins and sparse noise-driven islands;
- added rolling hills, rocky uplands and `core:high_mountains`; current default-seed regression sampling reaches above Y=300 without a vertical clamp;
- rivers are a dedicated warped 2D zero-contour stage: shallow surface subtraction, fluid fill, separate bank band;
- surface post-processing now owns beach, river-bank and river-bed materials;
- default sand is light yellow and gravel is dark grey;
- caves changed from a single threshold/blob field to intersections of thin warped 3D noise surfaces, yielding long connected spaghetti networks and junctions;
- `data/worldgen.json` is now nested into terrain/climate/caves/rivers/surfacePost sections and remains strict;
- `data/biomes.json` owns terrain offset/multipliers, ridge and sparse-island parameters;
- added `WorldGen::columnInfoAt()` for deterministic column-stage diagnostics;
- camera startup height samples the generated surface so tall terrain does not spawn the free camera inside a mountain;
- added regression tests for deep ocean, sparse island, >280-block mountain, river carve/fill, sand bank, river filament connectivity and 3D cave connectivity.

Harness validation: all 10 renderer-independent suites pass in both 16/16 and
32/32 configurations. The JSON-driven architecture gate passes and Clang's
static analyzer reports no diagnostics on the new worldgen/registry sources.
`clang-tidy` is not installed in this harness, so its project runner was not
executed here. Full SDL3/OgreNext target build remains a target-machine check.

## Architecture/static-analysis review v10 (2026-08-08)

- added mandatory `clang-tidy` AST/semantic analysis to the normal build path;
- added JSON-driven architecture rules plus include/module cycle and duplicate-header checks;
- renamed duplicate mesh `Chunk.h` to `ChunkMesh.h`;
- removed dead mesh indices and dead non-greedy fallback code;
- mesher now computes tangents only for normal-mapped block types;
- renderer reuses a persistent mesher and mesh scratch buffer;
- debug HUD formatting moved to renderer-independent `src/debug/`;
- HUD now distinguishes unloaded voxels from loaded `core:air`;
- removed unused input shutdown/log-level/container APIs;
- corrected the misleading fixed-step method name to `runFrameUpdate()`;
- added strict `data/worldgen.json` and removed hardcoded base terrain/fluid/surface-depth runtime values;
- resource JSON gains `chance`, and biome `resourceId` is now honored by worldgen;
- architecture checker currently reports no forbidden dependency cycle;
- renderer files remain the largest modularity debt (`ChunkWorldRenderer.cpp` and `OgreRenderer.cpp`).

See `docs/CODE_REVIEW_V10.md` and `docs/STATIC_ANALYSIS.md`.

## Last clean Git milestone

- HEAD: `64ccc7d` - milestone 05: chunked world storage with player-centered streaming
- last existing tag: `milestone-04-worldgen`


## PBS daylight/material follow-up (2026-08-08)

- terrain materials migrated from HLMS Unlit to HLMS PBS;
- directional warm sun + hemispherical ambient daylight added;
- PSSM sun shadows generated programmatically with PCF 3x3 filtering;
- chunk geometry now uploads tangents for real tangent-space normal maps;
- block JSON accepts `normalMap`, `normalMapStrength`, `roughness`, `metalness`,
  `reflection`, `reflectionMap`, `alphaMode`, `alphaCutoff`, `transparency`, `refraction`,
  `indexOfRefraction`, `receiveShadows`, and `castShadows`;
- shadow-casting and non-shadow-casting block sections are separated into at
  most two Ogre ManualObjects per chunk;
- water material uses explicit JSON transparency rather than duplicated colour alpha;
- `CLONECRAFT_BUILD_APP=OFF` permits renderer-independent CI/core testing
  without requiring SDL3/OgreNext on the test host;
- true screen-space refraction remains a dedicated-compositor follow-up; the
  current material path uses stable PBS transparency as fallback.

Harness validation: all 7 renderer-independent test suites pass for 16/16 Debug
and 32/32 Release configurations. A strict `-Werror -Wpedantic` build passes,
and ASan + UBSan + LeakSanitizer pass all seven suites. The sanitizer run exposed
and fixed signed-overflow UB plus process-lifetime heap leaks in the vendored
OpenSimplex2 port without changing its sampled output. Ogre-specific API usage
was cross-checked against current OgreNext 4.0 source/sample APIs; final GL3+/GPU
validation still belongs on the target Ubuntu/Ogre installation.

## Performance + sky follow-up (2026-08-08)

- runtime worldgen can write compact `uint16_t` BlockIds directly;
- streaming reuses one dense generation buffer and no longer round-trips every voxel through `std::string`;
- deterministic delta API no longer needs a sort pass;
- biome/resource runtime ids and resource total weights are resolved once per chunk;
- chunks cache non-air count and all-AIR chunks skip meshing entirely;
- >93% of EDGE=16 neighbour checks now stay on the dense local chunk array instead of hierarchical map lookups;
- greedy slice scratch moved from heap vectors to fixed-size arrays;
- steady-state renderer sync returns immediately when no chunk is dirty;
- Release builds stop HLMS shader-debug dumping;
- basic compositor sky changed from dark blue to daylight `#78A7FF`.

Measured in the same GCC14 harness before/after: string worldgen ~45.5% faster, radius-2 streaming ~52.8% faster, and meshing the loaded region ~62.2% faster (median). See `docs/PERFORMANCE.md`.

Behaviour checks: generated world hashes are identical to v2 across 648 chunks; mesh hashes are identical across a full radius-2 loaded region.

## Follow-up target-build repair

- fixed missing `BlockCoord`/`GroupCoord` value equality exposed by GCC 13 when comparing `std::map<BlockCoord,...>`;
- added a compile regression test for exactly that standard-container path;
- corrected a greedy-mesh unit test that ignored the intentional provisional side faces at unloaded chunk borders;
- tightened X11 OgreNext bridge to documented `display*:screen:windowHandle`;
- restored OgreNext ABI cookie checking and full resize notification;
- compile helper now has `--fingerprint` and prints SDL3/OS/kernel/toolchain versions.

## Repair completed

- removed broken greedy-mesh packed-atlas sampling path;
- data-driven block visual priority: texture -> colour -> diagnostic fallback;
- moved old hardcoded block RGB values into `data/blocks.json`;
- per-block Ogre materials/textures with local repeatable greedy UVs;
- transparent block material support using JSON alpha;
- SDL scancode array fixed to `SDL_SCANCODE_COUNT`;
- input key-repeat edge callbacks fixed; mouse delta precision preserved;
- runtime id 0 forced to `core:air` regardless JSON order;
- chunk/group size exposed through CMake compile definitions;
- greedy temporary grids no longer hardcoded to 16;
- 64-bit worldgen coordinate hash fixed;
- worldgen now generates one cubic X/Y/Z chunk, including negative Y;
- streaming now maintains a cubic X/Y/Z active region;
- loaded AIR is distinguishable from unloaded chunks;
- neighbour chunks are invalidated on load/unload/boundary voxel edits;
- duplicate resource delta ordering bug removed;
- biome selection changed from per-column hash randomness to continuous
  normalized OpenSimplex climate weights;
- StickyGroupAnchor handles very large one-frame group crossings;
- unloaded-neighbour faces remain visible at the streaming rim and remesh when
  the neighbour becomes known;
- unknown block ids no longer silently become AIR;
- negative terrain surface heights are no longer clamped to Y>=1;
- cave subtraction now applies after terrain/material/resource selection;
- biome filler/climate/resource-reference validation tightened;
- SDL focus loss clears pressed keys and platform shutdown is idempotent;
- duplicate platform event polling path removed;
- ChunkWorldRenderer detaches its ChunkManager callback during shutdown.

See `docs/CODE_AUDIT.md` for details and remaining issues.

## Validation in this repair environment

Passed:

```text
TestCoordinates: default 16/16 configuration
TestCoordinates: CLONECRAFT_CHUNK_EDGE=32, CLONECRAFT_GROUP_EDGE=32
renderer-independent C++ syntax checks for coordinates/chunk/meshing/worldgen
manual core validation: strict BlockId, streaming-rim culling, greedy UV/material id, negative terrain height, cubic radius-1 streaming
JSON syntax validation for data/*.json
GCC14 -Wall -Wextra -Wpedantic -Werror core builds
WorldGen/mesh behaviour hash comparison against previous v2
32x32x32 chunk-size WorldGen + ChunkMesh tests
```

The real target build is Ubuntu 24.04, GCC 13.3.0, CMake 3.28.3, OgreNext/HLMS
4.0.0unstable and nlohmann-json 3.11.3. The current external harness is
Debian-based and cannot be reimaged or fetch the required binary packages in
this runtime, so it is not falsely labelled an exact clone. Renderer-independent
tests are compiled here with stricter GCC 14; the target machine remains
authoritative for SDL3/OgreNext ABI/compiler validation. See
`docs/TARGET_BUILD_ENVIRONMENT.md`.

## Important remaining work

1. Add data-driven biome terrain-shape parameters and blend terrain height from
   the biome weight vector.
2. Keep validating the new hierarchical WorldPosition/render-anchor path on the target Ogre build; absolute float world coordinates are no longer used by camera/chunk nodes.
3. Verify the new per-block texture/material path against the installed
   OgreNext 4.0 build on the project machine.
4. Add a true fixed-step accumulator before Jolt/gameplay physics.
5. Jolt integration later: one local PhysicsContext, not one system per group.

## Build helper
A repository-local `compile.sh` is available. It performs environment checks,
CMake configure, parallel build and tests; `--run` starts the prototype after a
successful build. It never installs or changes OS packages. Use `./compile.sh
--help` for options, including experimental chunk/group dimensions.


### Runtime renderer follow-up
- Fixed incomplete OgreNext PBS HLMS shader library registration.
- Fixed generated colour texture binding to use `TextureGpu*` directly.
- Generated textures are marked `ManualTexture`.
- Tangent vertex data is emitted only for normal-mapped materials.

Runtime hotfix v6: corrected OgreNext HLMS path root. `HlmsPbs::getDefaultPaths()` returns paths relative to Media, so the renderer now resolves them from `.../Media/` rather than `.../Media/Hlms/`. HLMS setup exceptions are caught and reported instead of aborting.

## Lighting readability follow-up (v7)

- daylight hemispherical ambient increased after the first real target screenshot showed terrain collapsing into near-black silhouettes;
- added a camera-mounted PBS spotlight, enabled by default;
- `F` toggles the flashlight;
- enabled Ogre Forward3D with a modest 4x4x4 / 32-lights-per-cell budget so non-shadowed local lights participate in PBS;
- flashlight follows camera position/orientation exactly and uses a warm-white 72-block cone;
- flashlight shadow casting is intentionally disabled for now to avoid a second dynamic shadow-map path.


### Material colour-space audit (v8)
- Fixed missing sRGB framebuffer conversion for PBS output.
- JSON colours now map directly to PBS background diffuse in linear space.
- Diffuse textures remain sRGB inputs; normal maps remain linear.
- Added per-block material audit logging (source + PBR scalar values).
- This specifically addresses the runtime symptom: light gradients visible but block hue/albedo nearly black.

## Debug HUD follow-up (v9)

- `F5` toggles a Minecraft-style top-left debug overlay;
- displays latest/average FPS and frame time;
- displays world XYZ, group-local XYZ, and global coordinates of the local/group origin;
- displays block/chunk/group coordinates and local block-in-chunk;
- displays dominant biome plus normalized biome weights;
- displays current voxel id, loaded chunk/group counts, streaming counters, yaw/pitch and flashlight state;
- HUD text updates at 5 Hz to avoid per-frame biome/string allocation;
- Ogre Overlay dependency added explicitly;
- HLMS Unlit is now registered alongside PBS because Ogre font rendering uses Unlit;
- debug font is created from a TTF discovered in Ogre Media, so no font binary is bundled in the project.

### Debug HUD build follow-up (v9.1)
- Fixed GCC 13/14 incomplete-type failure from `std::unique_ptr<DebugOverlay>` in `OgreRenderer`.
- `DebugOverlay` remains forward-declared in `OgreRenderer.h` to preserve the renderer/UI module boundary.
- `OgreRenderer` constructor is now defined out-of-line in `OgreRenderer.cpp`, where `DebugOverlay` is complete.
- Regression reproduced with a translation unit that only includes `OgreRenderer.h` and calls `std::make_unique<OgreRenderer>()`; old header fails, fixed header compiles.

## Debug HUD resource fix (v10.1)

The F5 HUD now uses OgreNext's official debug resource layout first
(`2.0/scripts/materials/Common` + `packs/DebugPack.zip`) instead of guessing a
TTF under the HLMS tree. An existing Linux system TTF is a non-installing
fallback. Initialization logs now identify the exact font/resource stage that
failed.


## v18.2 — bundled JSON + crosshair block hover

- Bundled nlohmann/json 3.12.0 as `third_party/nlohmann/json.hpp`; CMake no longer
  probes or requires a system nlohmann-json package.
- Added strict `data/ui.json` configuration for crosshair texture/size/opacity
  and block-selection reach, RGBA, thickness, expansion and depth test.
- Added a textured centered Ogre Overlay crosshair.
- Added precision-safe hierarchical voxel DDA block picking.
- Added an Ogre HLMS Unlit geometric 12-edge selection outline with real
  configurable thickness rather than driver-dependent line width.
- F5 now reports the hovered block's stable/runtime id, display name, exact
  hierarchical address, ray distance, flags, texture and tags.
- Headless CMake build and all 13 tests pass; architecture check passes.
- The current repair container still has no SDL3/OgreNext development setup, so
  the graphical target was API-audited but cannot be linked/run in this harness.

## v18.3.5 — biome terrain height integration

- Removed the global +38/massif pedestal from `surface_height.lua`; the base
  lowland field now sits near Y=0 (nominal baseline around Y=8).
- Added data-driven `terrainMaskField` to biome definitions.
- `WorldGen` now blends `heightOffset`, `heightMultiplier`, `detailMultiplier`,
  ridge and island parameters from `biomes.json` into the shared surface field.
- Added a dedicated rolling-hills terrain mask.
- High mountains retain large explicit ridge amplitudes while plains/forest/
  desert no longer inherit mountain uplift merely from the global height field.
- The adjusted surface is used consistently by chunk passes, decorations,
  postprocess surface lookup and spawn-height queries.
- Regression tests cover biome terrain application and a default-world sanity
  raster containing both lowlands (<Y40) and mountain terrain (>Y120).

## v18.3.8 — natural river cross-section + cave sealing

- Replaced the five stepped lowland V-carvers with one continuous
  `river_valley_floor` profile. The open channel now blends U- and V-shaped
  geometry instead of producing a flat-bottom |_| canal.
- Added `river_bank_surface`: the first inner-bank blocks are normally at the
  Y=0 water surface, then rise irregularly to Y=1/Y=2 farther from the river.
- Added a varying `river_bed_surface` and `river_water_bottom`; water depth is
  greatest near the thalweg and becomes shallow toward each shore.
- Added post-cave river sealing. `river_substrate` lays a seven-block dirt plug
  below the bed and `river_bank_foundation` seals the inner banks, both allowed
  to replace cave Air. This prevents caves immediately below a river from
  opening holes through the channel.
- Added broad sediment noise and a new `core:clay` block/texture. The upper bed
  varies between sand, clay, gravel and dirt rather than using one gravel sheet.
- Stable seed-1337 regression probes verify a deep-vs-shallow bed cross-section,
  an inner bank whose sand surface begins at Y=0, and a real cave at Y=-10 that
  remains separated from the river by a sealed substrate through Y=-9.

## v18.3.7 — open-river V carve restored

- Restored the normal lowland river V-cut that was intentionally removed in
  v18.3.6 while fixing the floating-water/air-below-river regression.
- Extended generic `surface_layer` passes with optional `bottomField` and
  `bottomOffset`; this lets a pass carve from the real biome-adjusted terrain
  surface to an explicit lower boundary without reconstructing terrain height
  in Lua.
- The default lowland river uses five nested cuts ending at river Y offsets
  +8, +6, +4, +3 and +1. The centre therefore clears terrain above the water
  while never producing river-generated Air at Y<=0.
- Water, gravel and sand remain replace-only; the V carve stops above those
  layers instead of destroying their support.
- In real massifs the open-valley mask remains disabled and the existing
  upward/side tunnel chamber takes over.
- Regression probes cover both a stable lowland river centre and a bank column,
  plus a synthetic `bottomField` pass that proves the explicit floor is not
  crossed.

## v18.4.0 — staged worldgen pipeline + river vegetation decoupling

- Added explicit generic pass stages: `terrain` and `addon`. The runtime now enforces
  `terrain -> addon -> decoration` as a hard merge barrier; priority/JSON order only
  decide ordering within one stage.
- Marked base geology/surface passes as `terrain`; caves, river carve/tunnels, channel
  sealing, sediment and water are `addon`.
- Reworked `river_reeds` into a normal `surfaceMode: "postprocess"` decoration anchor
  over the final river-bank surface. It no longer assumes a fixed `river_level + 2`
  placement height.
- Sugar cane remains a generic `column` decoration and may root on shipped river-bank
  sand/clay/soil blocks. No river pass places or knows about sugar cane.
- Added regression coverage proving an addon with a lower numeric priority still runs
  after terrain, and that the shipped seed produces sugar cane along a postprocessed
  river bank.
