# COORDINATES

Clonecraft uses a hierarchical mixed-radix address so huge worlds never need a
flattened global block/chunk XYZ and never feed astronomical floating-point
coordinates into rendering, physics or world generation.

Phase 1 keeps the current edge sizes:

```text
BLOCKS_PER_CHUNK_EDGE   = CLONECRAFT_CHUNK_EDGE   = 16
CHUNKS_PER_GROUP_EDGE   = CLONECRAFT_GROUP_EDGE   = 16
GROUPS_PER_REGION_EDGE  = CLONECRAFT_REGION_EDGE  = 16
REGIONS_PER_SECTOR_EDGE = CLONECRAFT_SECTOR_EDGE  = 16
```

All discrete coordinate digits are `std::int64_t`. Physical storage radices
(Block->Chunk and Chunk->ChunkGroup) are deliberately separate from the logical
Region/Sector radices so a later phase can enlarge the super-coordinate radices
without making materialized chunks/groups enormous.

## Integer hierarchy

```text
SectorCoord          signed int64 X/Y/Z
  LocalRegionCoord   canonical 0..REGIONS_PER_SECTOR_EDGE-1
    LocalGroupCoord  canonical 0..GROUPS_PER_REGION_EDGE-1
      LocalChunkCoord canonical 0..CHUNKS_PER_GROUP_EDGE-1
        LocalBlockCoord canonical 0..BLOCKS_PER_CHUNK_EDGE-1
```

Production identities are:

```text
GroupAddress = Sector + Region + Group
ChunkAddress = GroupAddress + local Chunk
BlockAddress = ChunkAddress + local Block
```

There is no production `BlockCoord`, `ChunkCoord` or `GroupCoord` representing a
flattened world index. Carry/borrow is applied digit by digit with Euclidean
floor division/modulo. Negative movement therefore remains canonical and does
not inherit C++ truncating-remainder behaviour.

The top Sector digit is signed. An operation that would cross
`INT64_MIN`/`INT64_MAX` is rejected instead of wrapping.

## Continuous positions

`WorldPosition` is the authoritative continuous representation:

```text
BlockAddress                    exact hierarchical integer cell
fraction fx/fy/fz : float       0 <= f < 1
```

Movement may accept small double deltas, but it extracts whole-block carry and
immediately normalizes that carry through `BlockAddress`. It never constructs or
stores a global `double x/y/z`.

This keeps sub-block precision independent of the distance from the origin.

## Group-local mapping

A block can be mapped to the physical ChunkGroup-local range without flattening
the higher address:

```text
LocalGroupBlockCoord = 0..255 per axis   (with current 16x16 physical radices)
```

This small coordinate is the hot coordinate used by mapped worldgen noise. The
full `GroupAddress` is processed only when a group mapping/phase must be
prepared.

## Render anchor / floating origin

Ogre never receives an absolute world coordinate. `StickyGroupAnchor` owns a
nearby `GroupAddress` and the camera/chunks are converted relative to that same
anchor:

```text
camera renderer XYZ = WorldPosition relative to render GroupAddress
chunk renderer XYZ  = ChunkAddress relative to render GroupAddress
```

The result must be local before conversion to renderer floats. Hysteresis keeps
the sticky anchor from rebasing repeatedly around a group boundary.

## Worldgen precision

World-field Lua receives no world X/Y/Z. `MappedOpenSimplexNoise` folds the full
hierarchical GroupAddress into OpenSimplex2S's transformed lattice phase and
then samples from the small Group-local coordinate. Distance from the origin
therefore cannot consume `double` integer precision.

The finite OpenSimplex lattice is additionally bent by a very low-frequency
hierarchy-hashed macro warp. Its macro-grid identity is derived from address
digits rather than from a flattened coordinate.

## Phase-2 compatibility

Phase 1 ships Region/Sector radices of 16. The arithmetic is intentionally not
written around that small value. The coordinate/noise test core is also compiled
with logical Region/Sector radices of `9e18` and must still pass while the
physical Chunk/ChunkGroup sizes remain 16.

That check exists specifically so later super-coordinate expansion does not
require reintroducing a global world integer.

## Tests

The coordinate suite covers:

- positive carry through Block/Chunk/Group/Region into Sector;
- negative offsets and canonical floorDiv/floorMod behaviour;
- one-block deltas across Sector seams;
- sub-block fractions at Sector coordinates around +/-8e18;
- top-Sector overflow rejection;
- small 0..255 Group-local block mapping;
- alternate logical Region/Sector radices near the positive int64 range.
