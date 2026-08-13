# MILESTONES

Current roadmap: **OmniGrid Primus impetus** (master tracker #15, binding working
agreement `AGENTS_OMNIGRID_PRIMUS_IMPETUS.md`).

One milestone = one working Git state. Every milestone is committed and
documented on GitHub before the next one starts. Status: done ✔ / active ◐ /
planned ◻.

## Historical milestones (pre-Primus-impetus)

Kept as history; the roadmap below replaces them as the current plan.

- MILESTONE 00 — repository and environment baseline ✔ tag `milestone-00-baseline`
- MILESTONE 01 — SDL3 + OgreNext minimal renderer ✔ tag `milestone-01-renderer`
- MILESTONE 02 — core coordinate system ✔ tag `milestone-02-coordinates`
- MILESTONE 03 — data registries ✔ tag `milestone-03-registries`
- MILESTONE 04 — pure world generation ✔ tag `milestone-04-worldgen`
- MILESTONE 05 — chunk + ChunkGroup + streaming ✔ clean commit `64ccc7d`
- MILESTONE 06 — visibility + greedy mesh + terrain rendering ◐ never tagged;
  superseded by the Primus-impetus roadmap (renderer/streaming work continues in
  parallel lane P01 / issue #4, worldgen work in M02 / issue #8).

## Main sequence (Primus impetus)

- **M00 — Agent-/Dokumentations-Baseline ✔**
  `INDEX.plan` brought onto the OmniGrid/agent workflow (read-first order,
  Graphify rules, AST-vs-Graphify split, end-of-run gate); `docs/MILESTONES.md`
  synced with the current plan; `docs/STATUS.md` brought onto the current state.
  No persistent technical ids renamed cosmetically.
  Gate: behaviour/gameplay unchanged. Docs-only milestone.

- **M01 — Rename (#14) ◐**
  Safe branding/project surfaces only: README, window/application title,
  startup banner, developer docs, build/product names in small controlled
  steps. Persistent block/prototype ids, mod namespaces, save-schema ids,
  worldgen compatibility ids, future RocksDB keyspaces and protocol identities
  are compatibility contracts and are NOT renamed for branding.
  Status: a CloneCraft -> OmniGrid branding sweep exists uncommitted in the
  working tree; it needs to be finished, validated and committed as its own
  milestone commit.

- **M02 — Worldgen baseline (#8) ◻**
  Broaden desert climate distribution (shaped hot*dry response, no pure
  threshold lowering); define and prove `BiomeDef::weight` semantics; add
  determinism/statistics tests for seed 1337 and at least one additional seed;
  hierarchy/boundary tests; update worldgen docs. Then freeze the worldgen
  baseline again.

- **M03 — #3 Contracts + Prototype Foundation ◻**
  Ownership/dependency contracts; stable namespaced ids; generic
  world/block/object ref; content-root contract; `MODS/Default`; prototype
  registry; exactly one real pilot block. No full ECS/persistence/event world yet.

- **M04 — #3 Sidecar Pilot ◻**
  Generic sidecar framework; lazy allocation; lazy destruction; orientation as
  first pilot; empty/occupied tests; serialization/version metadata prepared.
  Temperature/Damage only after the proven orientation pilot.

- **M05 — #3 Unified World State ◻**
  `get` / `has` / `set`; prototype defaults; sidecar resolver; central block
  mutation; dirty hooks; mesh/neighbour invalidation; persistence-dirty
  abstraction without RocksDB. Lua/game code must not know whether a value
  comes from prototype, sidecar or later ECS.

- **M06 — #3 Minimal Actions + #18 Player Interaction ◻**
  Minimal actions `place_block` / `remove_block` with target/payload/result/
  handler/validation; then #18: voxel raycast, hit block/face/adjacent
  position, chunk boundaries, block remove/place, minimal creative selection.
  Required path: Input -> Raycast -> Action -> World State -> Dirty/Invalidation
  -> visible change. If input must mutate ChunkManager directly: STOP, fix #3.

- **M07 — #3 Events + #7 Lua Callback Cache ◻**
  #3: signal/slot registries, generalized action registry, payload schemas,
  validation, native + Lua handlers, bounded A/B queues, no-op creates no new
  event, cactus contact as second proof case. #7: `luaL_ref` callback handles,
  RAII/lifetime, `luaL_unref`, hot reload, stale-reference tests, error
  context, benchmark. Bytecode cache optional.

- **M08 — #3 Hot State / enTT ◻**
  enTT only as active projection; ECS mapping sidecar; promotion/demotion;
  state transfer; cold -> warm -> hot and hot -> warm; one furnace/test
  instance. Proof: public world-state API unchanged before/during/after
  promotion.

- **M09 — #3 RocksDB Persistence ◻**
  Backend-neutral interface; dirty tracking; serializer; schema versions; IO
  queue/worker; RocksDB; WriteBatch; stable keys; recovery. Required test:
  remove generated block, set player block, exit, restart, regenerate base
  world, apply deltas, both changes present. Gameplay code never knows RocksDB.

- **M10 — #13 Embedded Client/Server ◻**
  GameServer owns authoritative world state/ECS/persistence; ClientSession/
  ServerSession; transport interface; in-process/loopback; first transport the
  working #18 place/remove actions; remove the old direct transition;
  determinism/content handshake; client reconstructs base world, server
  delivers deltas. Standalone = client + embedded server; no other
  singleplayer path.

- **M11 — #16 Construction Foundation ◻**
  `get` / `set` / `setBulk`; bulk dirty/invalidation; chunk boundaries; logical
  properties; Lua `draw` abstraction with `fill` / `floor` / `wall` / `line` /
  `box` / `hollowBox`; one curved shape; writer abstraction. Proof: 100x100
  floor without 10,000 Lua -> C++ single calls. Then migrate exactly one real
  consumer.

- **M12 — #17 Construction Blueprints ◻**
  BlueprintWriter reuses #16 geometry (no second geometry engine); blueprint
  id/anchor/bounds; compact shape representation; A/B selection; 100x100 ghost
  floor; missing/fulfilled/conflict; chunk-aware ghost rendering; construction
  job; resource requirements; bounded task decomposition; generic test
  executor. No village/drone AI in the blueprint core.

## Parallel lanes (when local prerequisites are met)

One started milestone must be finished and committed before switching lanes.

- P01 — #4 Render/Streaming/LOD: surface metadata -> camera-aware queue -> LOD
  meshes -> LOD transitions/seams -> near/far residency split -> visibility
  metadata -> far worldgen preview. From M06 on, player block edits serve as
  invalidation/LOD regression tests.
- P02 — #11 Vegetation: context audit -> generic vegetation -> desert ->
  riparian -> underwater -> multi-block -> balancing. Reuse #16 where sensible.
- P03 — #6 RmlUi: UI base may start early; gameplay-mutating UI actions only
  via #3 actions, later via #13 server boundary. No UI -> ECS/sidecar/world
  direct mutation.
- P04 — #9 Audio: abstraction -> profiles -> 3D -> loops/buses -> Doppler ->
  environment -> limits. #18 place/break are ideal real event consumers.
- P05 — #10 Celestial: world clock -> body registry -> orbit -> sky space ->
  lighting -> moon phases -> multiple bodies -> seasons. After M10 shared world
  time is server-authoritative.
- P06 — #12 Fluids: not before usable sidecars, unified world state, bounded
  queues and persistence abstraction. Fluid state -> gravity -> horizontal
  balance -> `delta <= 1/8 = REST` -> bounded A/B queues -> overfill -> chunk
  boundaries -> persistence -> rendering -> mod-defined fluid. No own fluid DB,
  no ECS entity per fluid cell.
- P07 — #5 Villages: anchor -> terrain query -> village plan -> roads/plots ->
  procedural buildings via #16 -> cross chunk -> version/persistence ->
  aggregate society -> hot NPCs -> jobs via #17. No village-owned geometry
  engine.

## Not planned prematurely

Multiplayer networking, crafting completion, redstone clone, complex
liquids/GI/LOD, ragdolls, complex AI, custom renderer/physics/script language,
large ECS framework, editor, launcher or account system.
