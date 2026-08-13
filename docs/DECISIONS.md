# Architecture decisions

This file records decisions that future agents must not silently reverse.
The uploaded working tree is currently dirty beyond the last clean milestone;
read `INDEX.plan` and `docs/STATUS.md` before changing anything.

## ADR-001 - SDL3 owns the native application window
SDL3 creates and owns the main window and polls the event queue. OgreNext is
attached to that existing window and is the only renderer. No `SDL_Renderer`
is created.

## ADR-002 - OgreNext API follows the installed project version
Do not copy Ogre 1.x examples into OgreNext code. Inspect the actually installed
OgreNext headers/version on the development machine before changing renderer
APIs.

## ADR-003 - Clean ESC/window-close shutdown is an invariant
ESC and the native window-close request set application shutdown state and let
the normal ownership chain tear down renderer resources, Ogre and then SDL.
Normal user exit must not use `abort()`/`std::exit()`.

## ADR-004 - Chunks and ChunkGroups are cubic 3D regions
X/Y/Z are equal peers. There is no Minecraft-style fixed vertical chunk column
or hard world floor/ceiling in the coordinate model.

## ADR-005 - Chunk/Group edge sizes are compile-time experiment knobs
`OMNIGRID_CHUNK_EDGE` and `OMNIGRID_GROUP_EDGE` define the cubic edge sizes.
Algorithms must use the central constants and not hide literal 16/32 sizes.

## ADR-006 - ChunkGroups are sparse
A ChunkGroup is a coordinate/streaming/macro-world region. It does not allocate
all possible contained chunks or voxels up front.

## ADR-007 - AIR is explicit, unloaded is different
Runtime block id 0 is reserved for `core:air`. A loaded AIR voxel is not the
same state as an unloaded chunk. APIs that need the distinction use
`ChunkManager::tryBlockAt()`.

## ADR-008 - Persistent content ids are namespaced strings
JSON/persistent data uses ids such as `core:stone`; compact `uint16_t` block ids
are runtime implementation details. Unknown ids must fail loudly. They must not
silently turn into AIR.

## ADR-009 - Block visuals are data-driven
A block visual resolves in this exact order:

1. JSON `texture` when present and loadable;
2. JSON `color` when no usable texture exists;
3. renderer-owned magenta/black diagnostic texture.

The old C++ stone/dirt/grass/sand/water/gravel colour table is forbidden.

## ADR-010 - Greedy UVs live in block-texture space
A merged N x M face emits UVs spanning N x M. The active prototype gives each
block type its own repeatable material/texture section, so `TAM_WRAP` repeats
one selected block texture. Do not reintroduce packed-atlas wrapping, which was
the source of the texture-confetti bug.

## ADR-011 - Worldgen is deterministic and uses full 64-bit coordinate digits *(spatial part superseded by ADR-025)*
Chunk generation is a pure deterministic function of seed/config/registries and
its spatial address. No address digit may be narrowed to 32 bits. The former
flattened `BlockCoord` domain was replaced by ADR-025; top-level overflow is now
checked while carry/borrow proceeds through hierarchical int64 digits. Generation
produces one requested cubic chunk, not a whole vertical column.

## ADR-012 - Biomes are a continuous weight field
Biome suitability is sampled from continuous temperature, rainfall and
continentalness noise and normalized into weights. `biomeAt()` may choose the
dominant biome for a discrete material choice, but biome identity is not an
independent random pick for each column. Terrain height/detail and optional
ridge/island features are blended from biome-specific JSON terrain profiles.

## ADR-013 - Caves are negative geometry
Caves are a dedicated 3D subtraction stage after terrain/material/resource
selection. The cave field intersects pairs of thin, domain-warped implicit noise
surfaces to form line-like spaghetti networks instead of a single threshold
blob field. A configurable surface buffer protects top terrain layers.

## ADR-014 - Sticky group ownership uses hysteresis
`StickyGroupAnchor` retains an owner `GroupAddress` and only changes owner after
crossing a capture threshold. It consumes `WorldPosition`, never absolute
world-space doubles. Player/camera position is `BlockAddress + float fraction`,
and backends consume only coordinates relative to a nearby hierarchical anchor.

## ADR-015 - ChunkGroup is not a Jolt PhysicsSystem
When Jolt is added, interacting bodies from adjacent logical groups must share a
local `PhysicsContext`. A PhysicsAnchorGroup defines the local reference frame;
a logical group boundary must never become an invisible collision boundary.

## ADR-016 - JSON registries are strict
Unknown fields, invalid types/ranges, duplicate ids and invalid cross references
are errors with useful source context. A missing optional biome `fillerBlock`
defaults to its `surfaceBlock`. `temperature` and `rainfall` are constrained to
0..1. Optional biome `resourceId`, when present, must resolve after resources
load.

## ADR-017 - No OS package installation by coding agents
Agents may inspect the system and may use project-local dependencies under
`third_party/`, but must not run system package managers or install into
`/usr`, `/etc`, etc. Missing required system dependencies are reported and the
run stops.

## ADR-018 - Threading comes later at module boundaries
Do not build a giant job system prematurely. Worldgen/visibility/meshing remain
worker-friendly; SDL events/window lifetime and Ogre operations stay on the
appropriate main/render thread. A true fixed simulation accumulator is still
pending before Jolt/gameplay physics.

## ADR-019 - Worldgen tuning lives in JSON *(superseded by ADR-023 for active material generation)*
The normal application loads strict nested `data/worldgen.json`. Base/fluid
block ids, terrain/climate values, sea level, cave settings, river settings,
surface-post materials and surface depth are data-owned. Biome terrain/ridge/
island profiles live in `data/biomes.json`. `WorldGenConfig` keeps code defaults
for tests/explicit programmatic use, but runtime validates and uses JSON before
streaming.

## ADR-021 - Rivers are a surface-only post-terrain stage *(legacy v11 design; future post-process stage)*
Rivers are derived from a narrow band around a warped 2D zero contour, producing
continuous filaments. They subtract shallow terrain only from the column surface
and then fill fluid; they are not 3D negative geometry and do not use cave logic.
A wider contour supplies river-bank metadata for the later material-postprocess
stage.

## ADR-022 - Surface material repair is its own processing stage *(legacy v11 design; future post-process stage)*
Beach, river-bank and river-bed material rules run after terrain/river planning
and before cave subtraction. These transitions are data-owned via
`surfacePost`, keeping shoreline decoration out of biome height math and making
future post-processing rules composable.

## ADR-020 - Static analysis and module architecture are build gates
Normal `compile.sh` runs the JSON-driven project dependency checker and
`clang-tidy` AST/semantic analysis before compiling. Local agents do not install
missing analysis tools; they report the blocker. See `docs/STATIC_ANALYSIS.md`.

## ADR-023 - Data-driven field/pass world generation

**Decision:** Active terrain material generation is a generic runtime driven by
JSON pass definitions and Lua scalar fields. C++ owns sampling, parallel job
execution, proposal storage, deterministic merging and replacement-rule
validation only.

**Why:** Natural materials need independent spatial distributions, and adding a
new geology rule should not require editing a central C++ terrain switch. A
field/pass split also allows fields and proposal-producing passes to run in
parallel while preserving deterministic output through an ordered merge.

**Consequences:**

- blocks expose semantic tags for generic replacement rules;
- cave subtraction is represented by an Air-producing volume pass;
- Lua scripts define numeric fields and use deterministic native noise helpers;
- Lua is sandboxed away from filesystem/process/module APIs and its stock PRNG;
- JSON pass order is the tie-breaker for equal priorities;
- the former global-coordinate `TerrainSystem`, `RiverSystem` and `CaveSystem`
  compatibility path is removed; regression coverage belongs to the active field/pass runtime;
- biome terrain blending, rivers and decorations are now implemented on top of the same generic field/pass/anchor machinery; beaches and richer resource morphology remain future content layers.


## ADR-024 - Decoration uses immutable anchors and independent passes

**Decision:** Plants and multi-block structures are a second data-driven worldgen
stage after terrain, rivers, caves and material post-processing. JSON anchor
sets choose deterministic world positions. Decoration passes consume those
anchors using one of three generic operators: `scatter`, `column` or bounded
Lua `structure`.

**Why:** Grass/flowers are independent scatter work, while trees span many
blocks and chunks. Encoding tree species in C++ would recreate the central
worldgen switch ADR-023 removed. Making leaves wait on a wood worker would also
introduce order-dependent mutable state.

**Consequences:**

- anchor position/variant/seed depend only on world seed and integer anchor cell;
- wood and leaves may run concurrently and reconstruct identical branch topology
  from the shared immutable anchor seed;
- structure bounds define how far neighbouring anchor cells must be replayed so
  chunk generation order cannot cut structures at borders;
- decoration proposals merge only after terrain and addon stages have completed;
- `blocks.json` owns generic render shape (`cube`/`cross`) and texture selection;
- shipped vegetation textures are static data assets, not C++ generated images;
- C++ has no oak/birch/flower/sugar-cane identifiers or species-specific rules.


## ADR-026 - Worldgen mutation has hard terrain -> addon -> decoration stages

**Decision:** Generic block-mutating worldgen passes declare `stage: "terrain"` or
`stage: "addon"`. All terrain proposals are merged before any addon proposal, and
all addons are merged before decoration. `priority` and JSON order are deterministic
ordering keys only *inside* one stage.

**Why:** A single global priority list allowed unrelated systems to become accidentally
coupled. Vegetation must never need to know whether a river pass ran at priority 104
or 124, and a future cave/river/material tweak must not erase already-placed plants.
The semantic contract is geology first, terrain modifiers second, living/decorative
content last.

**Consequences:**

- base stone, topsoil and biome surface/cap material passes are `terrain`;
- deep dirt/gravel pockets, caves, open river cuts, mountain river tunnels, channel
  sealing, sediment, water and future ore/resource replacement are `addon`;
- vegetation and structures remain `decorations`, never terrain/addon passes;
- immutable decoration anchor discovery may be computed early for chunk reach tests,
  but `surfaceMode: "postprocess"` resolves the final staged terrain+addon surface and
  decoration blocks are merged only after both mutation stages;
- river sugar cane is a normal postprocess-surface decoration. River fields only gate
  where anchors are eligible; the river addon itself never places sugar cane.


## ADR-025 - Spatial identity is a hierarchical mixed-radix address

**Decision:** Production world/chunk/block APIs do not compose a global XYZ. A
spatial address carries a signed 64-bit Sector digit plus canonical int64 local
Region/Group/Chunk/Block digits. Movement uses checked carry/borrow through the
hierarchy.

Physical storage radices (`Block -> Chunk`, `Chunk -> ChunkGroup`) are separate
from logical super-coordinate radices (`Group -> Region`, `Region -> Sector`).
Phase 1 keeps all current radices at 16, but algorithms must not rely on the two
logical radices being small.

**Consequences:**

- `GroupAddress`, `ChunkAddress` and `BlockAddress` are the production identities;
- local coordinate types are explicitly named `Local*Coord`;
- streaming, meshing and decoration operate on address offsets instead of global XYZ;
- rendering consumes only a position relative to a nearby GroupAddress;
- Sector overflow is checked rather than wrapped;
- tests compile the same coordinate/noise core with logical Region/Sector radices
  at 9e18 as a Phase-2 compatibility guard.

## ADR-026 - v16 OpenSimplex is mapped from ChunkGroup-local coordinates

**Decision:** Keep the v16 OpenSimplex2S algorithm and its terrain morphology.
When a ChunkGroup is selected, fold the complete hierarchical Group origin into
OpenSimplex's transformed-lattice phase modulo its 2048 permutation period.
Normal `noise2`/`noise3` calls then use only 0..255 group-local block coordinates
plus small domain-warp offsets.

Lua receives no world X/Y/Z. Its native helpers take v16-like local scales:

```text
noise2(scale, salt [, offsetX, offsetZ])
noise3(scaleX, scaleY, scaleZ, salt [, offsetX, offsetY, offsetZ])
```

The mapper treats each scale's exact IEEE-754 value as a binary rational when
folding the huge integer address into lattice phase. No astronomical double is
formed.

A second, extremely low-frequency hierarchy-hashed X/Z coordinate warp bends
OpenSimplex slowly across many ChunkGroups. It uses shared hash control points
and quintic interpolation, so it does not create Group seams and prevents the
finite OpenSimplex permutation from becoming a simple cosmic wallpaper repeat.

**Why:** The v17 replacement hash-gradient noise was distance-safe but changed
river/terrain statistics and made the world look synthetic. It also put too much
hierarchical math into `noise3()`. The Phase-1 mapper preserves the desirable
v16 local noise while moving large-address work out of the voxel hot path.

**Consequences:**

- without macro warp, mapped 2D/3D noise matches v16 within about 2.5e-12 in the
  current numerical corpus;
- the shipped 24-field / 29-pass v16 worldgen formulas and river thresholds are retained;
- the macro warp intentionally moves large features slowly but leaves aggregate
  slope/river/cave statistics close to v16;
- 3D fields are X-sliced for worker-pool utilisation, without changing their sampled values;
- naive `worldX % period` before OpenSimplex is forbidden because the real
  periodicity lives in OpenSimplex's transformed lattice, not raw XYZ.

## ADR-027 - Unified world state owns all gameplay block mutation (M05)

**Decision:** `world::WorldState` (src/world/state/, module `world.state`) is
the single game-facing entry point for block and block-property state.
Gameplay/Lua code calls `has`/`get`/`set` by data-driven property id and
`setBlock` for block mutation. It resolves values against the
`SidecarRegistry`: `get()` falls back to the type's declared default (even for
absent/unloaded chunks; only unknown ids return nullopt), `set()` rejects
unknown ids, type-mismatched values and AIR positions and never creates
chunks, and default writes remove stored entries. All gameplay mutations flow
through WorldState into granular change hooks (`what` = `"block"` or the
property id) and a `PersistenceSink` (dirty-chunk/last-write-wins deltas in
the reference `MemoryPersistenceSink`; RocksDB backend in M09).

**Why:** Callers must never know whether a value comes from a prototype/
sidecar default or stored sidecar state (or, later, the M08 ECS hot layer).
Centralising mutation makes dirty tracking, change hooks and persistence
one code path instead of scattered direct ChunkManager writes; M06 actions and
M08 events depend on that. The M04 pilot showed direct chunk orientation
writes scale poorly once multiple sidecar types exist.

**Consequences:**

- Chunk stores all sidecar types generically
  (`std::map<std::string, std::unique_ptr<Sidecar<PropertyValue>>>`, keyed by
  the data-driven type id; std::map keeps serialization order deterministic
  for M09). No per-field sidecar members are ever added to Chunk.
- `PropertyValue = std::variant<std::uint32_t, float>` mirrors
  `SidecarDef::defaultValue`; the resolver enforces the value type declared in
  `sidecars.json`, so a sidecar never mixes alternatives.
- ChunkManager keeps `setBlockOrientation`/`blockOrientation` as convenience
  shims over the same `core:orientation` sidecar the unified world state uses —
  one storage, two views, verified by tests.
- ChunkManager `setBlock` returns whether the block actually changed; no-op
  writes are never dirty, never notify and never reach the sink.
- Worldgen base load (`assignBlocks` via the streaming manager) stays outside
  the unified mutation path — it is content loading, not gameplay mutation.
- The M04 review constraint stands: adding `mTemperature`/`mDamage`/`mPower`
  as more `unique_ptr` members to Chunk is forbidden.
