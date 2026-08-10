# Phase-1 coordinate / OpenSimplex validation

Date: 2026-08-10

This document records the A/B checks for the Phase-1 rebuild based on v16.5.
The goal is to replace global coordinates without replacing the v16 terrain
mathematics.

## Reference and candidate

Reference: v16.5, global int64 block/chunk coordinates feeding the original
OpenSimplex2S Lua helpers.

Candidate: hierarchical addresses plus `MappedOpenSimplexNoise`, using the same
OpenSimplex2S implementation and the same 24 fields / 29 passes. The candidate
adds only the slow hierarchy-hashed horizontal macro warp.

Both were built with `-O3 -DNDEBUG` in the same sandbox. Absolute target-machine
times will differ.

## Main-noise numerical equivalence

With macro warp disabled, 4,256 paired samples from X/Z -640..640 were compared
against the original v16 `NoiseSource`:

```text
2D max absolute error   1.3823e-12
2D mean absolute error  1.6916e-13
3D max absolute error   2.4318e-12
3D mean absolute error  2.2170e-13
```

These differences come from reordering the same linear transform around the
prepared lattice phase. They are far below terrain-visible scale.

The regression suite also checks continuity across a ChunkGroup boundary,
continuity of the hierarchy-hashed low-pass warp at its 32-Group macro-cell
boundary, and per-block variation at Sector coordinates around +/-8e18.

## Morphology with macro warp enabled

Eight 384x384 terrain windows spread over roughly +/-12k blocks were sampled.
The macro warp was enabled at the shipped 32-Group / 64-block settings.

| metric | v16.5 | Phase 1 |
|---|---:|---:|
| mean surface height | 44.0031 | 43.8941 |
| surface-height stddev | 9.6581 | 9.4762 |
| mean local slope | 0.298303 | 0.298451 |
| slope stddev | 0.145039 | 0.144976 |
| river `<0.024` coverage | 3.5152% | 3.4620% |
| river `<0.038` coverage | 5.5337% | 5.5382% |
| river `<0.055` coverage | 8.1125% | 8.1556% |
| river `<0.075` coverage | 11.0772% | 11.2419% |
| river `<0.100` coverage | 14.8106% | 15.2518% |

The macro warp changes where a particular hill or river lies, as intended, but
the aggregate slope and river-width distributions stay close to v16.

A larger dispersed cave sample of 1,048,576 voxels produced:

```text
v16 cave(<0.165) coverage      9.0848%
Phase-1 cave(<0.165) coverage  9.1685%
```

So the v16 spaghetti-cave density is retained within about one percent relative
in this sample. A permanent morphology regression also samples the shipped
`river_mask.lua` and `caves.lua`: the river must remain a long thin connected
filament rather than a flood mask, and the cave field must retain a large
connected 3D spaghetti component at the shipped threshold.

## Serial field sampling

One chunk, all 24 shipped fields, median hot timings:

```text
v16.5     12.188 ms
Phase 1   13.702 ms
```

The Phase-1 serial field phase is about 12.4% slower. Representative fields:

| field | v16.5 | Phase 1 |
|---|---:|---:|
| surface_height | 0.340 ms | 0.363 ms |
| deep_dirt | 1.392 ms | 1.581 ms |
| gravel | 2.047 ms | 2.239 ms |
| caves | 3.537 ms | 3.867 ms |
| river_mask | 0.107 ms | 0.111 ms |

The expensive hierarchical phase fold happens on Group/scale changes, not per
voxel. The remaining overhead is the local phase wrapper and low-pass warp.

## Parallel field phase

Production Phase 1 keeps the v17 experiment's useful scheduling change only:
three 3D fields are divided into 16 X slices each, exposing 48 jobs instead of
three field-sized jobs. Five interleaved process pairs gave these median field
phase timings:

| workers | v16.5 | Phase 1 | Phase-1 delta |
|---:|---:|---:|---:|
| 1 | 12.760 ms | 14.327 ms | +12.3% |
| 2 | 8.316 ms | 7.476 ms | -10.1% |
| 4 | 8.822 ms | 8.872 ms | +0.6% |
| 5 | 10.581 ms | 9.776 ms | -7.6% |

The sandbox has noisy shared CPU scheduling, especially above two workers, so
these are not hardware promises. They show that the small single-thread mapper
cost is largely recovered once the 3D work is exposed to more workers.

## Safety / architecture checks

Passed in the current harness:

- coordinate carry/borrow and Sector overflow tests;
- mapped OpenSimplex v16-equivalence test;
- ChunkGroup-boundary phase continuity;
- low-pass macro-warp continuity across its 32-Group cell boundary;
- huge Sector per-block variation;
- distant-address macro-warp de-periodisation;
- ChunkManager hierarchical neighbour test;
- streaming across a Sector boundary;
- WorldGen determinism at a huge Sector;
- architecture dependency/forbidden-symbol gate;
- strict touched-core compilation with `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Werror`;
- ASan/UBSan for the mapped-noise suite;
- ASan/UBSan for the focused WorldGen hierarchy/determinism suite;
- ASan/UBSan for the default river/cave morphology regression;
- alternate logical-radix build with Region/Sector radices at 9e18.

A complete CMake/Ogre target build remains a target-machine check in this
sandbox because `nlohmann/json.hpp` / the full target dependency set is not
installed here.
