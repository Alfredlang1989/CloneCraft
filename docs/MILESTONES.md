# MILESTONES

One milestone should correspond to one working Git state with a tag. The
uploaded prototype contains uncommitted work beyond the last clean commit, so
the current repair deliberately does not create a misleading milestone commit
or tag.

- Done ✔ / active repair ◐ / planned ◻

## MILESTONE 00 - repository and environment baseline ✔
Environment audit, git init, `.gitignore`, `INDEX.plan`, docs skeleton.
Tag: `milestone-00-baseline`.

## MILESTONE 01 - SDL3 + OgreNext minimal renderer ✔
SDL owns the native window; OgreNext renders into it. InputManager drains SDL
poll events. ESC/window close cleanly shut down; resize is forwarded to Ogre.
No `SDL_Renderer`. Tag: `milestone-01-renderer`.

## MILESTONE 02 - core coordinate system ✔
Cubic Block/Chunk/Group coordinate mapping, floor division/modulo for negative
coordinates, StickyGroupAnchor hysteresis. Coordinate tests include large and
negative values. Tag: `milestone-02-coordinates`.

## MILESTONE 03 - data registries ✔
Strict JSON Block/Biome/Resource registries, namespaced ids and cross-reference
validation. Tag: `milestone-03-registries`.

## MILESTONE 04 - pure world generation ✔
Deterministic OpenSimplex-based terrain/material/resource/cave generator.
Tag: `milestone-04-worldgen`.

## MILESTONE 05 - chunk + ChunkGroup + streaming ✔ (clean HEAD)
Clean Git HEAD `64ccc7d`: chunked world storage with player-centred streaming.
There is no milestone-05 tag in the uploaded repository, but the commit exists.

The current repair also corrects this area to true cubic X/Y/Z streaming,
materialises loaded all-AIR chunks, distinguishes loaded AIR from unloaded
world, and invalidates six neighbours when load/unload/boundary edits affect
visibility.

## MILESTONE 06 - visibility + greedy mesh + terrain rendering ◐
The uploaded tree already contained uncommitted milestone-06 camera/renderer/
mesh work. The 2026-08-08 repair fixes the packed-atlas/greedy-UV corruption,
moves block colours to JSON, adds texture->color->diagnostic fallback, removes
hardcoded 16x16 mesher scratch buffers and fixes outer streaming-rim faces.

This milestone is **not tagged/committed by this repair** because doing so would
mix pre-existing user/agent changes with the repair. It still needs a full
build/runtime check on the development machine with SDL3/OgreNext installed.

## MILESTONE 07 - block editing + deltas ◻
Select/remove/place, dirty chunks including neighbours, mesh/collision rebuild,
persistent delta storage.

## MILESTONE 08 - Jolt physics ◻
One local PhysicsContext with anchor, fixed timestep, terrain collision proxy,
player collision, cross-group dynamics and physics rebase. No per-voxel bodies.

## MILESTONE 09 - falling block prototype ◻
Triggered sand: static voxel -> FallingBlock -> Jolt -> land -> static voxel.

## MILESTONE 10 - entity system + Lua ◻
Small EntityManager, Lua 5.4 runtime, controlled engine API and entity callbacks.

## MILESTONE 11 - mod system ◻
`mod.json`, namespacing/dependencies, block/resource/entity definitions and Lua
scripts. Example mod must not modify core code.

## MILESTONE 12 - profiling + architecture review ◻
Measure worldgen/meshing/memory/draw calls/physics before optimisation and
record technical debt.

## Not planned prematurely
Multiplayer, crafting completion, redstone clone, complex liquids/GI/LOD,
ragdolls, complex AI, networking, custom renderer/physics/script language,
large ECS framework, editor, launcher or account system.
