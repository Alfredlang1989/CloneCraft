# Streaming, materialization and residency contract

This is the binding P01 domain contract derived from GitHub issues #4/#20 and
the accepted remote-main documentation commits. It is not a roadmap or status
ledger. Read it only for P01 or when a selected milestone changes streaming,
materialization, residency, LOD or hierarchy-residency properties.

## Four independent axes

Never collapse these into one `loaded` flag or one radius:

1. **Materialization:** which registered generation/content passes are ready;
2. **World/Data residency:** which logical world/chunk data remains in RAM;
3. **Simulation residency:** which areas/systems continue to simulate;
4. **Render residency/LOD:** which CPU/GPU representation is visible.

`not rendered != not simulated`. Simulation may require logical data without
requiring Ogre meshes or LOD0. Far LOD may exist without full simulation.

## Progressive materialization

Useful base terrain becomes eligible before late structures/decoration. Later
passes remain deterministic and invalidate only required chunks, meshes and
metadata. Render convenience must not expose logically invalid authoritative
state.

Conceptual stages such as `BASE_READY -> STRUCTURES_READY -> DECORATION_READY`
are allowed, but the exact stage names are implementation detail. The generic
core must not know content names such as flowers, trees or villages.

Under backlog, useful terrain coverage has priority over cosmetic completion.
Materialization state and render LOD are separate observable contracts.

## Per-pass policy

Every registered materialization pass uses stable namespaced identity and can
configure independently:

- load radius;
- keep radius;
- scheduling/materialization priority;
- retention/eviction policy;
- optional namespaced profile reference.

The invariant is `load_radius <= keep_radius`. Load radius makes missing work
eligible. Leaving keep radius normally makes existing state *evictable*; it is
not an immediate deletion command.

Explicit data-driven hard eviction outside keep radius is allowed for a
deliberate feature. It is never the default and never a C++ content-name
special case.

## Profiles and overrides

Streaming/performance profiles are open namespaced registry data, not a closed
C++ enum. Default content may ship balanced/performance/quality profiles; mods
may add others without new fixed Settings members.

Effective values resolve deterministically from content defaults, selected
profile and allowed user/runtime overrides. The user override wins last.

## Separate budgets and stable pressure handling

Keep at least three independent controls:

- generation/materialization work budget;
- world/data residency budget;
- render/GPU representation budget.

Profiles may use RAM, VRAM, frame-time/FPS, CPU/GPU utilization and
mesh/vertex/GPU representation targets where reliable metrics exist. A single
bad frame never triggers destructive eviction.

Pressure decisions require smoothed measurements, grace time, a trigger
threshold, a separate recovery threshold and recovery time. They must not
oscillate between evict/reload around one threshold.

Generic degradation can throttle low-priority new work, select cheaper render
representation, then evict low-retention state according to policy. The order
must not be hard-coded by content name.

## Sticky residency and LOD

Already-resident data survives beyond load radius and becomes evictable only
outside keep radius. Actual eviction considers policy/budget, distance,
last-use age, simulation/physics need, dirty/persistence state and useful render
representation.

Prefer degrading representation before disappearance where supported:

```text
LOD0/full -> simplified -> coarse/surface -> optional far preview -> none
```

Dirty authoritative state follows normal WorldState/persistence rules before
destructive eviction.

## Hierarchical properties and pins

Sparse Chunk/ChunkGroup/Section/Region/Sector facts and constraints reuse the
registered hierarchical Sidecar family. Do not create subsystem stores such as
`PinnedChunkTable` or `FactoryChunkState` when an existing canonical hierarchy
address plus property expresses the state.

Sidecars hold typed facts, not scheduler/SLA/render/factory policy. A required
residency constraint protects necessary logical state from normal radius or
pressure eviction, while render residency remains separately degradable.

Multiple independent owners/reasons must not collapse to one boolean. The
residency service owns token/refcount/owner lifecycle and restores derived
runtime constraints from persistent gameplay objects without orphaned pins.

## Mandatory proofs

1. two registered passes use different load/keep radii with no content-name
   logic in the scheduler;
2. leaving keep radius does not immediately delete healthy-budget state;
3. an explicit hard-eviction policy works through data;
4. a mod profile registers without a new fixed C++ Settings member;
5. user overrides deterministically change effective values;
6. sustained resource pressure degrades only after hysteresis/grace and
   recovers through a separate threshold/window;
7. rapid out-and-back movement avoids eviction/regeneration thrashing;
8. base terrain can be visible while late decoration remains pending;
9. render residency can disappear while required simulation remains active;
10. Chunk/higher constraints use hierarchy Sidecars and support multiple
    independent owners;
11. materialization, data, simulation and render/LOD states are separately
    observable in tests/diagnostics;
12. radii, priorities and budgets are data/config owned rather than frozen to
    the prototype's current chunk radius.

## Stop conditions

Stop and correct the architecture if generic C++ special-cases a pass/mod name,
load and keep remain the same immediate unload radius, pressure has no
hysteresis, profiles are closed enums, not-rendered means not-simulated,
simulation forces LOD0/Ogre residency, a parallel hierarchy metadata silo
appears, or one boolean lets one owner release another owner's hard pin.
