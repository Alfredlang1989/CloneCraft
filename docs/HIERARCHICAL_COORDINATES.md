# Hierarchical coordinates, Phase 1

Clonecraft does not use a flattened world-wide block or chunk XYZ in production
code. Spatial identity is a mixed-radix address:

```text
Sector      signed int64
  Region    local int64, currently 0..15
    Group   local int64, currently 0..15
      Chunk local int64, currently 0..15
        Block local int64, currently 0..15
```

Every discrete component is stored as `std::int64_t`. `SectorCoord`,
`LocalRegionCoord`, `LocalGroupCoord`, `LocalChunkCoord` and `LocalBlockCoord`
are intentionally separate types. The production identities are
`GroupAddress`, `ChunkAddress` and `BlockAddress`.

## No flattening rule

Code must not reconstruct an expression equivalent to:

```text
globalBlock = sector * sectorSize + region * regionSize + ...
```

Movement is performed by checked carry/borrow through the digits. Negative
movement uses Euclidean floor division/modulo semantics. Overflow of the signed
outer Sector is reported rather than wrapped.

The rule applies to streaming, chunks, meshing, decoration anchors and worldgen.
Rendering is different only in representation: the backend receives a small
position relative to a nearby `GroupAddress` anchor.

## Physical and logical radices

Chunk and ChunkGroup sizes are physical storage dimensions:

```text
BLOCKS_PER_CHUNK_EDGE = 16
CHUNKS_PER_GROUP_EDGE = 16
```

Region and Sector radices are logical address dimensions:

```text
GROUPS_PER_REGION_EDGE = 16
REGIONS_PER_SECTOR_EDGE = 16
```

Phase 1 deliberately keeps all four values at 16. The arithmetic does not rely
on Region/Sector being 16. A dedicated validation build was compiled with both
logical radices set to `9,000,000,000,000,000,000`; coordinate carry/borrow and
mapped-noise tests still passed. This is preparation only. The shipped Phase-1
configuration remains 16.

## Continuous positions

`WorldPosition` stores a `BlockAddress` plus sub-block fractions. The large
integer address never becomes a floating-point world coordinate. Movement first
normalizes the small fractional displacement, then carries whole-block movement
through the integer hierarchy.

## OpenSimplex mapping

Phase 1 retains the v16 OpenSimplex2S terrain morphology. The finite OpenSimplex
permutation is periodic in its transformed lattice, not in raw world X/Y/Z.
Therefore Clonecraft does not apply a naive `worldX % N` before the noise call.

When a ChunkGroup is selected, `MappedOpenSimplexNoise` folds the hierarchical
Group origin directly into OpenSimplex's transformed lattice phase modulo 2048.
The fold processes address digits and radices without constructing the flattened
integer. Scale values are small local doubles. Their exact IEEE-754 binary value
is decomposed and multiplied into the integer address modulo the required
power-of-two phase ring, using unsigned 128-bit intermediate arithmetic.

Inside that Group, normal noise sampling uses only:

```text
local group block coordinate 0..255
+ small domain-warp offset
+ prepared OpenSimplex lattice phase
```

`noise3()` therefore never sees an astronomical double and never re-runs the
mixed-radix hierarchy for each voxel.

## Low-pass macro warp

Native OpenSimplex2S still has a finite permutation period. To avoid a cosmic
wallpaper repeat, Phase 1 adds a very low-frequency horizontal coordinate warp.
Its control points are hashes of the complete hierarchical address. Adjacent
macro cells share control points and use quintic interpolation, so the warp is
continuous across ChunkGroup boundaries.

Current defaults are intentionally conservative:

```text
macro cell width: 32 ChunkGroups = 8192 blocks
horizontal amplitude: +/-64 blocks
direction: X/Z only
```

The low-pass field does not replace OpenSimplex. It slowly bends the coordinate
space in which the original v16 noise is sampled.

## Phase-2 constraint

Future logical super-coordinate radices may approach the positive `int64_t`
range. Phase-1 code is therefore required to:

- hash address digits individually;
- fold noise phase digit-by-digit modulo the lattice period;
- perform checked carry/borrow without first overflowing a local digit;
- keep physical Chunk/ChunkGroup dimensions independent of logical super-level
  radices.

See `PHASE1_VALIDATION.md` for the current numerical and performance checks.
