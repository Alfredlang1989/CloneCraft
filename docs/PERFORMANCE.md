# PERFORMANCE

Date: 2026-08-08

This document records measured prototype optimisations. The measurements are
microbenchmarks of renderer-independent engine code, not GPU frame-rate claims.

## Phase-1 mapped OpenSimplex A/B (2026-08-10)

Phase 1 was rebuilt from v16.5 and keeps the same 24 fields / 29 passes. A
serial all-field chunk sample measures ~12.188 ms on v16.5 and ~13.702 ms on
Phase 1, about 12.4% overhead for the huge-address-safe mapper and macro warp.

Production does not sample the three 3D fields as three monolithic jobs. Phase
1 splits each into 16 X-slices (48 jobs). Five interleaved A/B process pairs in
this shared-CPU harness produced median field-phase timings of 12.760/14.327 ms
(v16/Phase1) at one worker, 8.316/7.476 ms at two, 8.822/8.872 ms at four, and
10.581/9.776 ms at five. Scheduling noise is significant above two workers; the
important result is that the mapper cost does not force the multi-worker hot
path back to v17-level latency.

The mapped-noise fast path uses a per-salt call cache, a one-wrap OpenSimplex
lattice phase path, and precomputed macro warp values for each X-slice/Z column.
See `PHASE1_VALIDATION.md` for numerical morphology checks and full details.

## Benchmark environment

External OpenAI harness used for A/B comparison:

```text
Debian 13
GCC 14.2
C++20
-O3 -DNDEBUG -march=native
OMNIGRID_CHUNK_EDGE=16
OMNIGRID_GROUP_EDGE=16
same world seed/data for before and after
```

The real target machine remains Ubuntu 24.04 / GCC 13.3 / OgreNext 4.0.0unstable.
Absolute timings will differ there; the before/after algorithmic comparison is
still useful because both versions below were built and run in the same harness.

## Measured result

Median timings:

| workload | previous v2 | current | improvement |
|---|---:|---:|---:|
| generate 256 chunks through string/delta API | 109.657 ms | 59.786 ms | 45.5% faster |
| initial radius-2 3D streaming (125 chunks) | 54.280 ms | 25.615 ms | 52.8% faster |
| mesh all 125 loaded chunks | 45.893 ms | 17.365 ms | 62.2% faster |

The generated world and mesh geometry were hash-compared against the previous
v2 implementation:

```text
worldgen: identical across 648 chunks
mesh: identical across the full loaded radius-2 region
```

So these optimisations are intended to be behaviour-preserving.

## Implemented optimisations

### 1. Streaming no longer generates strings per voxel

WorldGen now has a runtime path:

```cpp
generateChunkIds(..., BlockIdTable, span<uint16_t>)
```

It writes compact runtime block ids directly into a reusable dense chunk buffer.
The streaming path no longer does:

```text
voxel -> std::string block id -> BlockDelta -> hash lookup -> uint16 id
```

for every solid voxel.

The string/delta API remains available for persistence/tests and uses the same
core generation result.

### 2. No worldgen sort pass

The compatibility delta API now walks the dense result directly in deterministic
x/y/z order. It no longer emits unsorted deltas and then calls `std::sort()`.

### 3. Biome/resource runtime data resolved once per generated chunk

Biome surface/filler ids and resource block ids are resolved to compact ids once
per chunk. Resource total weight is also calculated once instead of once per
X/Z column.

The hot dominant-biome calculation no longer allocates a `BiomeWeight` vector
and strings for every terrain column. The public `biomeWeightsAt()` API remains
unchanged for inspection/tests.

### 4. Streaming generation scratch is reused

`ChunkStreamingManager` owns one chunk-sized `uint16_t` scratch buffer and
reuses it for each generated chunk instead of allocating one output container
per chunk.

### 5. Chunks cache their non-air count

`Chunk` maintains `mNonAirCount`. Empty chunks can therefore be detected in O(1).
This is especially useful for the real 3D streaming model, where many loaded
chunks above the terrain can be entirely AIR.

WorldGen returns the non-air count with the compact generation result, so the
streamer can bulk-copy data without a second counting pass.

### 6. Empty chunks skip meshing entirely

`ChunkMeshBuilder` immediately returns for an all-AIR loaded chunk instead of
running six directional sweeps over it.

### 7. Intra-chunk neighbour checks bypass ChunkManager

For EDGE=16, 93.75% of directional neighbour checks are inside the same chunk.
Those now read the dense voxel array directly.

Only actual chunk-boundary neighbours use the hierarchical
`ChunkManager -> ChunkGroup` lookup.

This removes tens of thousands of map/hash lookups from a typical chunk mesh.

### 8. Mesher scratch allocations removed

The per-slice greedy `grid` and `taken` buffers are fixed-size `std::array`
scratch buffers. At EDGE=32 they are still only a few KiB total.

The opaque-runtime-id lookup is cached once per `ChunkMeshBuilder`.

### 9. Mesh output keeps only data consumed by the renderer

`ChunkMesh` keeps the greedy quad vertex stream and reserves a sensible
surface-sized capacity. The old CPU `indices` vector was removed because the
Ogre upload path always emits the same two triangles from each four-vertex quad
and never consumed those stored indices.

The renderer also reuses one `ChunkMeshBuilder` and one mesh scratch buffer
across dirty chunk rebuilds, preserving cached block properties and vector
capacity instead of recreating them per chunk. Tangent math is skipped for block
types that have no normal map.

### 10. Streaming evicts before generating the incoming slab

On a chunk crossing, chunks outside the new radius are removed before the new
slab is generated. Final world state is unchanged, but the transient loaded
chunk/memory peak is lower. Rendering sync happens after the streaming update,
so the reordering does not expose an intermediate frame.

### 11. Renderer steady-state work reduced

`ChunkWorldRenderer::sync()` is called every frame but now returns immediately
when no chunks are dirty. Material grouping during a rebuild uses dense runtime
block ids instead of a tree map.

### 12. Release HLMS debug output disabled

Ogre HLMS shader debug-output dumping is kept in debug builds only. Release
builds avoid that extra shader-development file I/O.

## Still intentionally not optimised yet

- one Ogre ManualObject material section per block type per chunk;
- synchronous worldgen/meshing on the main thread (resolved by the 2026-08-09 follow-up below);
- optimize render-anchor rebase cost further if profiling shows group-boundary node updates matter;
- texture array/bindless material path;
- true fixed-step simulation;
- Jolt physics.

Those are larger architectural milestones and should not be mixed into a small
correctness/performance patch.

## 2026-08-09: flight-stutter / Lua field-pass follow-up

The field/pass rewrite exposed a new frame-time problem: `ChunkStreamingManager`
was still synchronous. With application radius 3, moving by one chunk introduces
one 7x7 slab = 49 chunks. The frame that crossed the boundary generated all 49,
then meshed/uploaded the resulting dirty set before rendering.

Measured in the same external GCC14 `-O3 -march=native`, EDGE=16 harness:

| workload | v12 field-pass | current v13 | change |
|---|---:|---:|---:|
| generate radius-3 incoming 49-chunk slab | 171.662 ms | 56.628 ms | 67.0% less CPU time |
| main-thread streaming `update()` on one-chunk crossing | ~entire generation cost | 0.058 ms | generation removed from frame |
| background completion of that 49-chunk slab | n/a | 64.631 ms total | no single-frame stall |

The current data set is heavier than v12 (it also evaluates desert/river masks
and applies twelve passes instead of six), so the CPU reduction is not from
removing terrain features.

Implemented changes:

1. Lua VMs are reused per evaluator/per worker thread instead of recreating the
   VM, standard libraries, script and native NoiseSource cache for every field
   and chunk.
2. WorldGen uses a persistent batch worker pool; no new `std::jthread` group is
   constructed twice per generated chunk.
3. 2D fields run first. Provably all-Air sky chunks return before any expensive
   3D geology/cave field is sampled.
4. Replacement rules are precompiled to dense runtime-block-id masks. The merge
   no longer performs string-tag hash lookups for every proposal.
5. Pass merge order is sorted once at world initialization, not once per chunk.
6. Chunk generation is asynchronous. The background streamer queues nearest
   chunks first and discards stale results after a camera replan.
7. Only four completed chunks are materialized into `ChunkManager` per frame by
   default. Generated buffers are recycled and the ready queue is bounded.
8. `ChunkWorldRenderer` rebuilds/uploads at most six loaded chunk meshes per
   sync; unloaded objects are still destroyed immediately.
9. Debug HUD now exposes queued and ready chunk counts so streaming backlog can
   be observed while flying.

ASan/UBSan probes pass for the new field/pass generation and asynchronous
streamer. A 1-worker vs 5-worker comparison was bit-identical across a
7x8x7 chunk region.

## 2026-08-10: hierarchical-noise group mapping (v17.2)

The first hierarchical-noise implementation paid the mixed-radix coordinate
division/hash cost inside nearly every noise call. v17.1 cached some prefixes
and split the three 3D fields into 48 X-slice jobs, but its single-sample
`noise3()` hot path remained expensive.

v17.2 resolves the hierarchical address once at the ChunkGroup boundary and
uses `LocalGroupBlockCoord` (0..255 per axis) for normal sampling. Zero-offset
noise also prepares a chunk-local frame containing the 16 axis positions plus
the small lattice-gradient set touched by the chunk. Domain-warp calls retain
the exact group-local mapping and do not flatten world coordinates. A direct
v17.1/v17.2 dump over normal and +/-8e18 Sector samples, including warped 2D/3D
calls, is bit-identical.

Representative field-sampling times in the external EDGE=16 harness. Each cell is the median of three process runs, each process reporting its median over 40 chunk samples:

| workers | v16 | v17.1 | v17.2 |
|---:|---:|---:|---:|
| 1 | 8.42 ms | 20.65 ms | 12.68 ms |
| 2 | 5.49 ms | 10.49 ms | 6.80 ms |
| 4 | 3.72 ms | 5.70 ms | 4.85 ms |
| 5 | 4.21 ms | 5.74 ms | 3.36 ms |

The v17.2 data set also removes three redundant 2D river cut-depth fields by
replacing the former five discrete valley bands with one continuous cut-depth
field, so the end-to-end field workload is slightly smaller than v16/v17.1. The
remaining single-worker gap is concentrated in domain-warped 3D noise. At four
to five workers v17.2 is in the same practical range as v16 in this small
harness, with normal scheduler variance; the important comparison is the large
recovery from v17.1 without flattening coordinates.
