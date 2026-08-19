# Architecture decisions and rationale

This file is supporting rationale, not the current architecture authority.
`docs/ARCHITECTURE.md` is the non-negotiable contract and
`docs/ROADMAP.md` owns current milestone numbering/status. Historical milestone
labels below are preserved only to explain when a decision originated; they must
not be used to infer current roadmap order. Read this file only when a selected
milestone or concrete finding needs the rationale.

## ADR-031 - CommunicationRuntime owns the messaging semantics (M03 Round 1)

**Decision:** `world::communication::CommunicationRuntime` is the single
production communication bus. It owns the unique message-id sequence, the
Signal/Slot/Action registries, the typed payload schemas, per-slot output
contracts, the bounded A/B queues, the delivery routes and the capability
authorization grants. `CommunicationRouter` becomes the execution/routing
mechanism only; its route map is the internal binding table. The runtime
exposes no raw router or raw `MessageIdSource` access - ids are minted
exclusively through the guarded `nextMessageId()` and the controlled
`makeReply(cause, result)`.

**Why:** M02 proved the envelope+router contract; M03 generalizes it to
Event/Query/Reply, multiple producers and later a timer worker. Without a
single owner, several systems would drift back into parallel buses
("one communication contract"). The reviewer chain (Senior Review, M03
Round 1) rejected raw-id and raw-router bypasses: a trace sink could consume
ids, steal outputs or execute handlers outside the controlled path.

**Consequences:**

- Command/Event/Query/Reply use the same `CommunicationEnvelope`; no second
  event object is ever introduced;
- `SignalRegistry` (signal -> payload schema), `SlotRegistry` (receiver/
  context/capability -> expected schema + bound action + declared
  `OutputContract`) and `ActionRegistry` (namespaced id -> executable) are
  real registries; registration is all-or-nothing (atomic);
- `OutputContract` (maxOutputs, output kind, output payload schema,
  correlation requirement) is validated on BOTH delivery routes; the async
  path pre-flights capacity BEFORE the handler runs (peek-based), rejects
  permanently undeliverable messages at `submit()` and never drops silently;
- every message is delivered on exactly one route: synchronous `dispatch()`
  returns outputs in the `DispatchResult` (A/B untouched); the async
  `submit()/pump*()/nextOutput()` path delivers exclusively through queue B;
- the async pump is non-reentrant (`AsyncPumpGuard`) and the runtime trace is
  a read-only observer (exceptions contained; a sink cannot consume outputs,
  inject messages, dispatch, pump, mint ids or mutate runtime configuration);
- capability strings in envelopes are ROUTING requests; authorization comes
  only from registered grants (`grantCapability`) - content-driven capability
  resolution from receiver/prototype/slot properties is deferred to a later
  M03 round;
- the Round-2 timer worker must use a thread-safe handoff to the owner thread
  and only `submit()` due envelopes onto queue A - it never calls
  `submit()/dispatch()/pump*()`, Lua, WorldState or the renderer from its own
  thread.

## ADR-032 - The delayed-message scheduler stores only transportable envelopes (M03 Round 2)

**Decision:** `world::DelayedMessageScheduler` is a timer, not a bus and not a
gameplay runtime. Its stored unit is exactly
`due_time + monotonic sequence + CommunicationEnvelope` - never
`std::function`, callback pointers, Lua refs, WorldState refs or handler refs.
It reinterprets no message field. Time is abstracted behind `SchedulerClock`
(`now`/`waitUntil`/`interrupt`): production uses `SteadySchedulerClock`
(`std::chrono::steady_clock`), tests inject a deterministic fake clock.
Delivery is bounded and head-of-line, errors are loud, and introspection
reads scheduler-owned state - never the live `std::thread` object.

**Why:** A timer must not become a second event system. The M02/M03 review
chain rejected function-pointer/callback timers because a stored `luaL_ref`
or `std::function` cannot be transported (M06 client/server), persisted
(historical WorldState work) or replayed deterministically; the envelope is the permanent
transportable unit. `steady_clock` makes a running timer immune to OS time
corrections. A bounded, head-of-line handoff keeps the owner-thread A-queue
authority (ADR-031) without an unbounded retry structure. Touching
`std::thread` from introspection would race `join()`.

**Consequences:**

- the worker thread NEVER calls the runtime, Lua, WorldState or the renderer
  and never runs gameplay handlers; it only pops due entries into a bounded
  thread-safe handoff. The owner/game thread drains via
  `drainDueTo(runtime) -> CommunicationRuntime::submit()` onto the existing
  inbound A queue;
- interrupts are pending-safe: one armed immediately before `waitUntil()`
  still aborts that wait (consumable pending flag, no busy waiting), so a
  new earlier schedule can never be lost to a freshly entered wait;
- `(due_time, sequence)` ordering: scheduling calls are linearized under the
  scheduler mutex and each receives a monotonic sequence, so same-due-time
  serial producers keep exact call order; genuinely concurrent producers
  have no cross-run guaranteed relative order (their order is the mutex
  linearization order, which is not reproducible across runs);
- head-of-line backpressure: a front that cannot be submitted stays in the
  bounded handoff and the drain stops immediately - no loss, no message-id
  re-issue, no unbounded retry queue, no overtake; a permanent submit
  rejection (broken contract) removes the poisoned front, preserves all
  successors in order and throws a defined `CommunicationError`;
- loud errors: sequence overflow throws (never wraps); `scheduleAt`/
  `scheduleAfter` after `shutdown()` throw (silent loss is forbidden);
  `handoffCapacity == 0` is rejected by the constructor; shutdown stops and
  joins the worker (no detached zombie);
- introspection (`running()`, `workerThreadId()`) reads scheduler-owned state
  under the scheduler mutex and never touches the live `std::thread` object:
  `running()` derives from the scheduler's own stop flag (the worker exits
  solely through it) and `workerThreadId()` reads a cached id captured at
  worker start - a parallel `shutdown()`/`join()` can therefore never race a
  read;
- Round 3 gameplay Lua and the Round 4 two-block proof still run through the
  SAME `CommunicationRuntime`/envelope contract; the scheduler adds no
  gameplay semantics.

## ADR-033 - Gameplay Lua is one owner-thread runtime with per-script isolation and an uncatchable instruction budget (M03 Round 3)

**Status:** implemented as `world::scripting::GameplayLuaRuntime`
(module `world.scripting`), fresh closure verified 2026-08-19 and independent
reviewer PASS confirmed by the project owner; not yet committed. The reviewer
transcript is not bundled in this Nightrun archive, so no synthetic review tag
is recorded.

**Decision:** Gameplay content scripts run in ONE Lua 5.4 state on the
owner/Game thread only. Every script gets its own `_ENV` and its own shallow
copies of the sandbox namespace tables (`bus/world/math/string/table/utf8`);
the real `_G`, shared type-metatables and raw `os/io/debug/package/require/
dofile/loadfile/load/coroutine` (plus `rawset`, `rawget`, `getmetatable`,
`math.random/randomseed`) are unreachable. Scripts communicate exclusively
through the existing `CommunicationRuntime` and `DelayedMessageScheduler`;
WorldState is read-only (`world.get_block`). Outbound `sender` is always the
host `ScriptBinding` principal, message ids always come from the runtime, and
`message_id`/`correlation_id` reach Lua as opaque uint64 decimal strings. The
instruction budget uses a Lua count hook installed exactly once per state;
a budget abort is an UNCATCHABLE host abort (private sentinel +
`InvocationContext.budgetExceeded`; budget-aware `pcall`/`xpcall` re-raise it;
a handler-local abort cannot recursively re-enter that handler or expose the
sentinel, while ordinary handler failures retain stock Lua 5.4 recursion/
LUA_ERRERR). Wire/contract
tables must be plain data tables (metatables rejected) with strict boolean/
unknown-field validation.

**Why:** Gameplay scripts must not freeze the game thread (budget), must not
spoof bus identity (host principal + runtime ids), must not mutate the world
or bypass the one communication contract, and must not leak state between
mods/scripts (isolation). Stock `pcall`/`xpcall` would turn a budget abort
into a swallowable error, and a script-provided `errfunc` would leak the
private sentinel to Lua - hence the budget-aware wrappers with real Lua 5.4
result semantics. Plain-data contract tables keep the strict codec free of
metamethod magic. Opaque decimal strings keep full uint64 message/correlation
identity without Lua's signed-int64 wrap.

**Consequences:**

- the GameplayLuaRuntime is owner-thread enforced on EVERY public API; Lua is
  never called from other threads and never under a mutex;
- per-script namespace copies: a script sabotaging `bus.send`/`math.abs`/
  `table.insert`/`string.format`/`world.get_block` can only hurt itself;
- the budget hook is installed once at construction - nested queries cannot
  re-arm the countdown; the budget applies per invocation (top of the
  invocation-context stack), with recovery after any budget error;
- budget aborts are NOT normal Lua errors: a body abort never executes the
  error handler; a handler-local abort executes it once only and cannot be
  recursively delivered back to it; the sentinel is never handed to or
  storable by Lua, and the abort propagates until the outer host `invoke()` throws
  `GameplayLuaBudgetError`;
- sandbox `pcall`/`xpcall` behave like stock Lua 5.4 for normal errors and
  results (including omitted/non-callable protected targets, function args,
  error-handler results, and no handler leak into results);
- read-only envelope snapshots reject every write (proxy with protected
  metatable); `rawset`/`rawget` are absent from the sandbox, so the proxy
  cannot be bypassed;
- `loadScript` rejects duplicate script ids (no hot reload in Round 3);
- Round 4 (visible two-block proof) and later gameplay run through the SAME
  runtime/bus; the runtime adds no gameplay semantics itself.

## ADR-034 - The two-block bus proof is data-driven content over the one communication contract (M03 Round 4)

**Status:** repair candidate implemented 2026-08-19; targeted M03 automated
suites are green. Independent `compile.sh` review and the manual visible Alfred
gate remain pending and are not claimed by the builder.

**Decision:** The mandatory visible two-block proof is expressed as data-driven
content plus generic integration, never as block-name special-casing:

- `MODS/Default/gameplay.json` names script files, bus handlers, normally
  materialized placements and the initial invocation. The generic
  `GameplayContentRuntime` loads that manifest without knowing A/B actions,
  content ids or colours;
- the generic registered command `core:property.set` carries a namespaced
  property id plus typed value and mutates only through WorldState;
  `test:visual_tint` stores packed `0xRRGGBBAA` and `test:callback_count` stores
  the proof counter, with all proof values and behavior in shipped Lua/content;
- B's exact address travels in the typed `BlockTargetPayload`. `replyTo` is
  reserved for logical reply correlation and is never a coordinate carrier;
- bootstrap placement waits until streaming/worldgen materializes every target
  chunk. Explicitly declared occupied-cell replacement uses the ordinary
  `core:block.remove` then `core:block.place` bus commands; it never creates an
  empty chunk or bypasses WorldState;
- `BlockDef::visualTintProperty` is an optional generic registry projection.
  Mesh vertices record the exact voxel that emitted each quad, so tint lookup
  is correct on negative and positive X/Y/Z faces rather than guessing with
  `floor(firstVertex)`.

**Why:** The proof must demonstrate the one communication contract (envelope /
router / scheduler / Lua runtime / WorldState) end-to-end, not a bespoke
two-block hack. Data-driven content keeps the renderer and the bus generic and
reusable; Sidecars are the authoritative visual/callback state; the scheduler
still transports only plain `CommunicationEnvelope`s; A reaches B only through
one routed bus Event.

**Consequences:**

- no second event object, no parallel bus, no `event_id -> vector<callbacks>`;
- no direct A->B C++ call; Lua mutates only via `bus.send(Command)` ->
  WorldState; `world.get_block` stays read-only;
- the renderer tint is keyed by the canonical emitting block address and its
  registered property value, never by a block name;
- automated acceptance loads the shipped manifest and Lua files instead of
  maintaining a second embedded copy;
- the manual visible gate is separate Alfred acceptance on the graphical target
  and is not faked by headless tests.

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

## ADR-027 - Unified world state owns all gameplay block mutation (historical WorldState work)

**Decision:** `world::WorldState` (src/world/state/, module `world.state`) is
the single game-facing entry point for block and block-property state.
Gameplay/Lua code calls `has`/`get`/`set` by data-driven property id and
`setBlock` for block mutation. The world state is **prototype-aware** (M05
review gate): a property exists for an object only when the object's prototype
declares it in `prototype.properties` (prototypes.json, including the
prototype-specific default). `has` answers the logical capability question
"does this object support property X" - not "is an override stored"; `get`
resolves stored override -> prototype default -> sidecar type default;
`set` stores a per-block override of the prototype default and enforces the
declared sidecar `valueType`/`bitWidth` at runtime. All gameplay mutations
flow through WorldState into granular change hooks (`what` = `"block"` or the
property id), boundary neighbour invalidation, and a `PersistenceSink`.

**Why:** Callers must never know whether a value comes from a prototype
default, stored sidecar state or, later, the M08 ECS hot layer. Centralising
mutation makes dirty tracking, change hooks and persistence one code path
instead of scattered direct ChunkManager writes. The prototype gate makes
the API a *logical object state API* (an object owns the properties its
prototype declares) rather than a generic sidecar write-anywhere API: plain
scenery blocks, AIR and unloaded chunks own no properties. M06 actions and
later events depend on that. The historical orientation pilot showed direct chunk orientation
writes scale poorly once multiple sidecar types exist.

**Consequences:**

- Chunk stores all sidecar types generically
  (`std::map<std::string, std::unique_ptr<Sidecar<PropertyValue>>>`, keyed by
  the data-driven type id; std::map keeps serialization order deterministic
  for persistence (current M05)). No per-field sidecar members are ever added to Chunk.
- `PropertyValue = std::variant<std::uint32_t, float>` mirrors
  `SidecarDef::defaultValue`; the resolver enforces the value type *and
  bitWidth* declared in `sidecars.json`, so a sidecar never mixes alternatives
  and never stores a value wider than its declared encoding.
- Prototypes declare supported properties + prototype defaults in
  `prototype.properties` (PrototypePropertyDef), validated at load time.
  `get()` uses the prototype default before the sidecar type default; a
  prototype default that does not fit the sidecar type falls back to the
  sidecar default.
- ChunkManager keeps `setBlockOrientation`/`blockOrientation` as convenience
  shims over the same `core:orientation` sidecar the unified world state uses —
  one storage, two views, verified by tests.
- ChunkManager `setBlock` returns whether the block actually changed; no-op
  writes are never dirty, never notify and never reach the sink. Boundary
  block *and* property changes invalidate the adjacent chunks
  (mesh/neighbour invalidation).
- `PropertyDelta` records carry the final property value (nullopt = the
  override no longer exists: a default write or a block replacement removed
  it). Sidecars with `persist: false` never reach the sink.
- ChunkManager APIs stay public as the physical storage/test surface; the
  semantic gates (prototype capability, type/width, AIR, no chunk creation)
  live in WorldState. Gameplay must not call ChunkManager mutation directly —
  M06 enforces this with its first real consumer (hard stop if input/UI has
  to mutate ChunkManager directly).
- Worldgen base load (`assignBlocks` via the streaming manager) stays outside
  the unified mutation path — it is content loading, not gameplay mutation.
- The generic Sidecar constraint stands: adding `mTemperature`/`mDamage`/`mPower`
  as more `unique_ptr` members to Chunk is forbidden.

## ADR-028 - Prototype-aware property removal is write-order independent (historical WorldState review)

**Decision:** A sidecar's "writing the default removes the override" decision is made
against the *object's own logical default* supplied per write (`Sidecar::setWithDefault`),
never against a chunk-wide baked default. Two prototypes may share one sidecar type in
the same chunk with different logical defaults; whichever object first created the
sidecar cannot change how another object's values behave. `Chunk::setBlock` clears a
replaced block's sidecar entries with an explicit `remove()`, never by writing a
(possibly wrong) stored default.

**Why:** Prototype-specific defaults are the point of the prototype-aware
resolver (a property value is meaningful relative to the object that owns it). If the
sidecar baked in the first writer's default, a second prototype writing that same
numeric value would see it "equal to the default" and drop a real override. That is a
deterministic, write-order-dependent world-state bug — exactly the class that becomes
poisonous once mods share one engine (M06+).

**Consequences:**

- `WorldState::set()` always passes the object's logical default
  (prototype default if it fits the sidecar type, else the sidecar type default) as the
  removal threshold; the stored sidecar default is only a fallback for the typed
  orientation pilot (`Sidecar::set`).
- `Chunk::setBlock` invalidates a replaced block's sidecar state with explicit removal,
  so no zombie entry can survive at a value that meant something under the previous owner.
- Regression: two prototypes sharing `mod:p` with defaults 0/1 in one chunk keep both
  overrides regardless of write order (TestWorldState).

## ADR-029 - Prototype properties are cross-validated against sidecars.json at load time

**Decision:** Prototype property declarations are validated against `sidecars.json` at
load time: every declared property id must resolve to a registered sidecar type, and the
prototype default must fit that type's declared `valueType`/`bitWidth`. `WorldState::has`
also refuses property ids that resolve to no sidecar type, so `has()`/`get()`/`set()`
cannot diverge even for content loaded without the gate. `WorldState::setBlock` rejects
runtime ids outside the `BlockIdTable` and never materializes a chunk for a vacuous AIR
write on an unloaded position.

**Why:** ADR-027 claimed "validated at load time" for prototype properties, but the parser
had no sidecar registry at parse time and the Application loaded prototypes before
sidecars. A prototype could declare `mod:missing` with no backing sidecar, producing
`has()==true / get()==nullopt / set()==false` — a broken mod reported as a working one.
`setBlock` accepting any `uint16_t` (e.g. `65535`) let the central validated mutation
store corrupt voxel data, and an AIR no-op on an unloaded position created empty chunks.

**Consequences:**

- Sidecars are loaded before prototypes; `loadPrototypes`/`parsePrototypes` accept an
  optional `SidecarRegistry` and reject unknown property ids and non-fitting defaults
  with source context (a broken mod fails loudly, not silently).
- The runtime `has()` sidecar-resolution check keeps the API self-consistent for
  programmatic registries that skip the parse gate.
- `WorldState::setBlock` validates the runtime id against `BlockIdTable::size()` and
  `ChunkManager::setBlock` treats AIR-on-unloaded as a vacuous no-op (no chunk
  materialized); non-AIR writes still create the chunk (the materialisation API).
- Regression tests: prototypes gate rejection + default-mod validation, has()/get()/set()
  consistency for a declared-but-unresolvable property, setBlock invalid-id rejection,
  and AIR no-op residency.

## ADR-030 - Hierarchical Sidecars are sparse properties on canonical world addresses

**Status:** implemented in M01-B, commit `a848de9` (#20). Implementation notes
below document the decisions taken during delivery.

**Decision:** The existing block-local Sidecar family is extended to registered
properties targeting Block, Chunk, ChunkGroup, Section, Region and Sector.
Chunk-level metadata is keyed by `ChunkAddress`, not by a reserved local block
slot. ChunkGroup/Section/Region/Sector metadata is keyed by the exact canonical
hierarchical identity already owned by the coordinate system.

Section, Region and Sector remain logical address tiers. Adding metadata must
not materialize Section/Region/Sector containers or flatten identity into a
global integer/double.

A registered Sidecar definition includes an allowed target scope (`scope` is a
mandatory field of sidecars.json; missing/unknown values are a
`RegistryError`). The same typed value, default-removal, sparse/lazy, `persist`,
bit-width and serialization-version rules apply across scopes.

**Why:** World-scale systems need sparse metadata such as residency constraints
and mod-owned regional facts without proliferating subsystem-specific metadata
databases. The logical coordinate space is too large to preallocate hierarchy
objects, and a fake block index for Chunk metadata would make scope ambiguous.

**Consequences:**

- one scope-aware WorldState target/API is preferred over six independent stores;
- scope is part of validation and persistent logical identity;
- untouched hierarchy addresses allocate no property state;
- Section/Region/Sector property writes do not create ChunkGroups or Chunks;
- no implicit parent->child inheritance exists unless a future explicit generic
  mechanism defines it;
- Sidecars store typed facts/constraints, while residency/simulation/render
  services implement policy;
- sparse state expressible through #20 should not create parallel stores such as
  `PinnedChunkTable`, `FactoryChunkState` or `RegionFlags`;
- M02 should reuse the scope-aware target for communication addressing.
