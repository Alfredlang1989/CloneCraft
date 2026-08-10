# Architecture

## Goal

A modular, data-driven 3D voxel sandbox with deterministic, unbounded world.
Feature mass is secondary; architecture quality, small verifiable steps,
documentation for later LLM agents, and a working git state per milestone
are primary.

## Stack / dependency assumptions

- Language: C++17 minimum, C++20 preferred for application code
- Build: CMake, Ninja
- Window/Input: SDL3. The uploaded development-machine build metadata points
  at an SDL3 installation, but agents must re-audit the actual machine rather
  than assuming a version/path.
- Renderer: OgreNext. The uploaded prototype was built against OgreNext 4.x-era
  APIs; inspect installed headers/pkg-config before changing renderer code.
- Physics: Jolt (not installed yet; intended project-locally under
  third_party/, fixed timestep)
- Scripting: Lua 5.4 (not installed as dev package; only runtime .so present;
  planned project-local build under third_party/)
- Config/data: JSON via bundled header-only nlohmann/json 3.12.0 under `third_party/nlohmann/`
- Noise: v16 OpenSimplex2S behind a hierarchical ChunkGroup phase mapper, plus a very-low-frequency hierarchy-hashed coordinate warp; world addresses are never flattened into global floating point

## Ownership and module layout

Application owns all subsystems and their lifetime; it does not own the
engine logic itself.

    src/
        app/            Application (lifecycle, main loop)
        core/           Config, Logging
        platform/       PlatformWindowBridge (SDL window native handle for Ogre)
        input/          InputManager (SDL_PollEvent mapping)
        debug/          renderer-independent diagnostic snapshot formatting
        ui/             strict data-driven UI configuration
        render/         OgreRenderer + Ogre presentation backend
        world/          coordinates, blocks, chunks, interaction/picking, groups, streaming,
                        generation, meshing, persistence
        physics/        PhysicsContext (Jolt anchored world)
        entity/         EntityManager
        scripting/      LuaRuntime (controlled engine API)
        mods/           ModManager (manifest/JSON discovery, namespaces)

## Main loop

    while (running) {
        input.pollEvents();          // full SDL event queue drain
        application.runFrameUpdate();  // prototype currently variable dt
        world.processCompletedJobs();  // streamed worldgen results
        renderer.renderFrame();        // independent render rate
    }

The current camera update uses clamped variable frame delta. The method is now
named `runFrameUpdate()` so it no longer claims to be a fixed simulation step.
A true accumulator/fixed simulation tick remains a pending invariant before
physics/gameplay simulation.

Shutdown is ordered: stop loop -> drop gameplay objects -> drop Ogre
resources/window -> destroy Root -> destroy SDL window -> SDL_Quit.

## Window ownership

SDL owns the native window and all window state. OgreNext never creates its
own native window; it is attached to the SDL window via a platform bridge.
No SDL_Renderer ever exists. Gameplay code never sees HWND/Display/surfaces.

Implementation status: milestone 01 delivered Application + InputManager +
PlatformWindowBridge (X11) + OgreRenderer (OgreNext 4.0.0 GL3+ attached to
the SDL window, external-window mode, clear-colour workspace). See
docs/RENDERER.md for binding and resize details.

## World model

Conceptually gigantic 3D. Spatial identity is hierarchical: `Sector(int64) -> Region(int64 local) -> Group(int64 local) -> Chunk(int64 local) -> Block(int64 local)`. No global BlockCoord/ChunkCoord exists. Continuous positions are `BlockAddress + sub-block fraction`; rendering uses only a small position relative to a sticky `GroupAddress` anchor. World-field noise folds the hierarchy into the OpenSimplex transformed-lattice phase once per ChunkGroup and samples from small group-local coordinates. A hierarchy-hashed low-pass warp prevents a simple finite-permutation wallpaper repeat without replacing the v16 local noise morphology.

## Decision / conflict notes

Where code and docs disagree, docs are updated after verifying actual state.
System dependencies are never modified; missing libs are built project-locally
under third_party/ or documented in docs/MISSING_DEPENDENCIES.md.
## Automated architecture gate

`tools/architecture_check.py` reads `tools/architecture_rules.json` and runs on
every normal `compile.sh` invocation. It hard-fails forbidden project include
directions, module cycles, project include cycles and duplicate header
basenames. Large translation units are reported as warnings. `clang-tidy` is
the complementary C++ AST/semantic pass. See `docs/STATIC_ANALYSIS.md`.

## Data-driven worldgen boundary

`world.worldgen` is an execution engine, not the owner of geology semantics.
The active chunk path loads Lua-backed scalar fields and generic JSON pass
definitions, evaluates fields/passes concurrently, and deterministically merges
`BlockProposal` arrays. Concrete rules such as stone, topsoil, gravel or cave
subtraction live in `data/worldgen.json` and `data/worldgen/*.lua`.

The registry owns block semantic tags used by replacement rules and the biome
terrain profiles. `WorldGen` receives the immutable `BiomeRegistry` once during
construction, resolves each biome's `terrainMaskField` to a 2D worldgen field,
and blends the biome terrain parameters into the shared surface field before
passes run. Chunk streaming itself still only asks `WorldGen` for a dense
runtime-ID buffer and owns no biome/material-selection logic.

World generation has a hard semantic stage barrier: base `terrain` passes run first,
then `addon` passes mutate that completed terrain (caves, rivers, tunnels, sealing,
sediment, future geology addons), and only then does the runtime execute the separate
data-driven decoration phase. Immutable anchor sets are generated from 2D fields, then
`scatter`, `column` and bounded Lua `structure` passes run independently and are
merged deterministically. Multi-pass structures such as tree wood/leaves share
only immutable anchor coordinates/seeds, so workers never exchange mutable
structure state. Cross-chunk reach is declared by structure bounds.
