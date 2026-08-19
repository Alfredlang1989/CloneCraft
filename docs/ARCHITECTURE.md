# OmniGrid architecture contract

## Authority

This file is the canonical, non-negotiable architecture contract for OmniGrid.
Every planner, builder and reviewer MUST read it completely before acting.

If source code, a milestone plan, an old ADR, a report, a test shortcut or a
historical document conflicts with this contract, the contract wins. Do not
silently reinterpret it. Stop planning or review and report the conflict.

Detailed domain documents may refine this contract but may not weaken it.
`tools/architecture_rules.json` is the executable dependency firewall and must
be kept aligned with this contract as modules are added.

## 1. Core philosophy: mechanisms in C++, concepts in content

The C++ core is deliberately dumb.

C++ may own generic mechanisms such as storage, routing, scheduling, numeric
runtime IDs, serialization, geometry primitives, physics integration and
render projections. It MUST NOT know gameplay concepts merely because current
content uses them.

Forbidden in generic production C++:

- hard-coded game/content IDs or branches such as `test:block_a`, `ore:iron`,
  `pickaxe`, `tnt`, `tree`, `red_block`, etc.;
- gameplay-specific meaning encoded in generic Sidecar, ECS, transport,
  renderer or persistence infrastructure;
- special treatment for the bundled/default game that ordinary mods cannot use.

Persistent content identity is a stable namespaced string at content/storage
boundaries. Runtime C++ may map aliases to compact numeric IDs and operate on
those IDs. Runtime numeric IDs are not automatically stable persistence or
network identities.

`core:air` / runtime block id 0 is the explicit engine-level AIR exception.
AIR does not mean "chunk unloaded".

## 2. Content and Lua ownership

Gameplay/content belongs outside the generic engine mechanisms.

Target ownership is two Lua/content layers:

1. a generic developer/API layer reusable by every game/mod and containing no
   game-specific semantics;
2. game-specific logic belonging to the active game and its mods.

The target content layout is `/Game/Core/` for the active game's own logic and
`/Game/Mods/` for mods. The current `MODS/Default` tree is transitional legacy
layout, not a privileged "vanilla" architecture. New generic engine features
must not depend on `Default` semantics.

Lua may request mechanisms through the public API and the one communication
contract. Lua does not receive mutable raw engine internals, ChunkManager
mutation access, raw Sidecar stores or renderer/physics ownership.

Future data-driven component/property systems must remain registerable without
hard-coding gameplay component names into the C++ core. Implementation details
may use typed C++ internally, but the architecture must leave a registry/schema
boundary for game/mod-defined semantics.

## 3. Technical namespace policy

Namespaces/categories stay coarse and technical, not gameplay-taxonomic.
Examples of the intended style are:

- `Worldgen::` for world-generation algorithms/passes;
- `Core::` only for core mechanisms;
- `Block::` for block types/content bridges;
- `Item::` for item types/content bridges;
- `Entity::` for living/dynamic entity mechanisms.

Do not create namespaces merely because a game concept sounds like a category.
Plants, for example, are blocks when represented as blocks; they do not need a
special engine namespace merely for being plants.

Existing lowercase/source-layout namespaces are not required to be renamed by
an unrelated milestone. The rule constrains future architecture and refactors.

## 4. One authoritative world state

There is one authoritative logical world-state language.

`WorldState` is the game-facing authority for persistent/logical voxel state
and sparse properties. Gameplay must not mutate normal world state by reaching
around it into ChunkManager, Sidecar containers, ECS projections, renderer
objects, Jolt bodies or persistence backends.

Authoritative mutation path:

`producer -> CommunicationEnvelope -> router/handler -> WorldState`

Worldgen/materialization may use its documented base-world loading path, but
must not become a second gameplay mutation API.

Sidecars are sparse typed facts/constraints attached to canonical addresses.
They store state, not policy. Schedulers/services interpret them. Do not invent
parallel special-purpose state stores when an existing registered hierarchical
property expresses the same fact. Registered content may declare generic
persistence/residency requirements for properties that must remain present for
systems such as power networks; that requirement is metadata/policy input, not
a reason to build a subsystem-specific state database.

Spatial Sidecar scopes are canonical Block, Chunk, ChunkGroup, Section, Region
and Sector identities. Section/Region/Sector remain logical sparse address
tiers and MUST NOT imply gigantic materialized containers.

Global game/world state such as world time, day cycle or astronomy is a
separate World-scope concern. Do not fake global state by attaching it to an
arbitrary Sector/Region/block.

Fast query/index mechanisms such as "does this chunk contain block ID X?" and
"return local positions of block ID X" may be derived/cache structures, but
must not become competing authoritative block stores.

## 5. Projections are not authorities

The voxel/logical world remains authoritative. Other systems are projections:

- enTT: hot/active ECS projection;
- Jolt: active physics projection;
- OgreNext: render projection;
- client render/cache state: presentation projection.

A projection may cache or derive data needed for its job. It may not become a
second source of truth for persistent gameplay state.

Promotion into a projection and demotion back out must preserve the logical
state contract. Persistent identity must not be a raw `entt::entity`, Jolt body
handle, Ogre object pointer or other process-local handle.

## 6. One communication contract

`CommunicationEnvelope` and the one communication runtime/router are the
permanent communication model.

The contract carries Command, Event, Query and Reply semantics with stable
fields for sender, receiver, context, action, target, payload, message ID and
correlation/reply semantics.

Do not create a second gameplay event object, action envelope, physics bus,
fluid queue, UI bus, timer callback channel or gameplay-specific network
message universe.

The current `CommunicationRuntime` may later be extracted/renamed into the
standalone `OmniComBus` framework. That extraction must make the bus more
generic, not create a second bus or pull OmniGrid/game semantics into the
framework. OmniGrid consumes the communication mechanism; the mechanism must
not depend on OmniGrid content concepts.

`replyTo` is a logical reply address. It is not spare payload space for block
coordinates or arbitrary data.

Producers such as input, Lua, UI, Jolt contacts, timers and network sessions
must enter the same logical routing/mutation architecture. They may have
transport/adaptor code, not alternate gameplay semantics.

## 7. Scheduler and threading rules

Authoritative gameplay mutation and communication dispatch occur on the owner
/game thread unless a later milestone explicitly defines a safe ownership
change without violating this contract.

Workers may perform bounded independent work, I/O, timing or analysis. They
must hand results back as transportable data/envelopes. Worker threads must not
hold or call Lua references, gameplay callbacks, mutable WorldState references,
renderer objects or other owner-thread state.

The delayed-message scheduler stores transportable envelopes, never function
pointers, `std::function`, Lua references or WorldState references. Due messages
re-enter the normal communication runtime on the owner thread.

Threading is introduced at module boundaries, not by sprinkling shared mutable
state across existing classes.

## 8. Coordinates and huge-world precision

Persistent spatial identity is hierarchical integer addressing:

`Sector -> Region -> Section -> ChunkGroup -> Chunk -> Block`

Carry/borrow is checked digit by digit. Outer overflow rejects; it never wraps.
Do not flatten astronomical world coordinates into one global integer or
float/double XYZ.

Persistent world storage, worldgen, chunks, streaming and meshing use the
hierarchical integer model.

Dynamic objects use local floating-point `DynamicSpace`. The explicit
World/Dynamic bridge owns conversion. Player/NPC/vehicle/projectile/future Jolt
bodies that interact in one active island share that local space and rebase
transactionally.

The Ogre render anchor is a separate presentation origin and may rebase more
often. ChunkGroup, DynamicSpace, render anchor and future Jolt physics islands
are different concepts.

## 9. Rendering and physics boundaries

SDL3 owns native window/event polling. OgreNext is the renderer. Do not add an
SDL renderer or leak Ogre types into world/core modules.

Renderer state is derived from authoritative world state. Visual features may
use generic registered visual data, but the renderer must not contain
content-specific block/game semantics.

Jolt integration is future active physics projection. Normal voxels must not
become one Jolt body/entity per block. Static terrain collision is derived in
coarse/greedy structures appropriate to the active physics region. Collision
configuration is data-driven with sparse overrides where required.

Jolt contacts that need gameplay effects enter the same communication router.
No physics-special gameplay bus.

## 10. enTT boundary

M04 introduces enTT only as active/hot projection. WorldState and the
communication contract stay semantically stable.

Persistent/logical entity identity is distinct from raw `entt::entity`.
Promotion/demotion owns the mapping and must be lossless for persistent state.

ECS must not become the hidden authoritative store for normal voxel blocks or
persistent Sidecars.

## 11. Persistence boundary

Persistence is behind a backend-neutral interface. RocksDB is an implementation
backend, not a gameplay API.

Persist stable logical identities and versioned schemas. Do not persist raw
process handles, raw `entt::entity`, renderer/Jolt handles or runtime-only
numeric IDs without a stable mapping contract.

Base world plus deltas and sparse Sidecars must survive restart. Writes that
form one logical change need crash-consistent batching semantics.

Timer persistence requires an explicit durable time model. Never persist raw
`steady_clock::time_point` values.

I/O workers do I/O/serialization work and hand completed data back through
owned queues/contracts. They do not mutate gameplay state from worker threads.

## 12. Client/server boundary

The server owns authoritative WorldState, active ECS authority/projection
coordination and persistence.

Standalone singleplayer is client + embedded server. The client must not bypass
the server merely because both live in one process.

Transport carries the same CommunicationEnvelope semantics. In-process,
loopback and future network transports differ only in transport mechanics.
No gameplay-specific `NetworkMessage` universe may appear beside the bus.

## 13. Construction boundary

Construction uses one geometry/shape spine and one writer/mutation spine.
Blueprints must reuse M07 shapes/writer rather than creating a second geometry
engine.

Large operations cross the Lua/C++ boundary as compact operations, not one Lua
call per block. Bulk writes still respect WorldState validation, dirtying,
invalidation, chunk boundaries and communication ownership.

## 14. Residency and future systems

Materialization, World/Data residency, Simulation residency and Render
residency/LOD are separate states. `not rendered != not simulated`.

Load/keep policy, budgets and pressure handling require hysteresis/grace rather
than immediate load/evict oscillation. Sparse metadata should reuse the
hierarchical property architecture where appropriate rather than create a
parallel metadata database.

The full binding P01 rules for progressive per-pass materialization,
data-driven load/keep/retention policy, independent budgets, pressure recovery
and multi-owner residency constraints live in `docs/STREAMING_RESIDENCY.md`.

Future fluids, villages, vegetation, astronomy, UI, audio and physics must plug
into the same state/communication architecture instead of growing subsystem-
specific world truths.

## 15. Executable architecture gates and ownership

Architecture is checked in complementary ways:

- `tools/architecture_rules.json` + `tools/architecture_check.py`: deterministic
  module/dependency/source-pattern firewall;
- Graphify: architecture/dependency knowledge graph;
- clang-tidy/static analysis: C++ semantic/AST gate;
- compiler/tests: behavior and integration.

Graphify is architecture tooling. In the OpenCode harness the PLANNER owns
`graphify update .`. Builder and reviewer MUST NOT refresh the graph.

`graphify update .` runs exactly once after each builder implementation pass and
before its reviewer pass. The reviewer may query/read that refreshed graph but
never updates it.

`compile.sh` is the final independent acceptance gate and belongs to the
REVIEWER. Builder and planner MUST NOT run it. Builders use targeted builds and
tests while implementing.

A green test does not overrule an architecture violation. Do not weaken a gate
or architecture rule to make a patch pass. Change the patch.

## 16. Hard forbidden shortcuts

Stop and report rather than implementing any of these:

- second world-state/mutation path;
- second gameplay bus/event/network semantics;
- direct Input/UI/Lua/Jolt -> ChunkManager mutation;
- Timer worker -> Lua/WorldState/gameplay handler call;
- persisted scheduler callbacks/Lua refs/function pointers;
- raw `entt::entity` or engine-object handles as persistent public identity;
- normal voxel -> one ECS/Jolt entity per block;
- flattened huge-world float/double identity;
- giant Section/Region/Sector containers for sparse metadata;
- content-specific production C++ in generic engine modules;
- privileged Default/vanilla-only engine features;
- separate geometry engines for construction and blueprints;
- Singleplayer path that bypasses embedded server authority;
- changing tests/checkers/contracts merely to silence a valid failure.

## 17. Architectural target

The intended spine is:

```text
Producers: Input / Lua / UI / Timer / Jolt / Network
                         |
                CommunicationEnvelope
                /       |       \
            Command    Event   Query/Reply
                |        |          |
             handlers / systems / Lua
                |
             WorldState
          /       |        \
    Prototype   Sidecars   hot enTT projection
                |
          Persistence adapter

Timer due       -> same router
Jolt contact    -> same router
Network receive -> same router
```

One world state. One communication contract. Multiple transports and
projections. Content semantics stay out of the generic core.
