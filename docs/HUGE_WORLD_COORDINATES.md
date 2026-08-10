# Huge world coordinates

Clonecraft Phase 1 uses hierarchical addresses rather than global block/chunk
coordinates. See `HIERARCHICAL_COORDINATES.md`.

The shipped radices remain deliberately modest:

```text
Sector      signed int64
Region      16 per Sector
Group       16 per Region
Chunk       16 per Group
Block       16 per Chunk
```

That already gives 65,536 blocks per Sector edge while the outer Sector itself
uses signed int64.

The important property is not today's numerical size but the absence of a
flattened intermediate coordinate. Region and Sector are logical address levels,
not allocated containers. Phase 2 may therefore raise their radices close to the
positive int64 range without making a ChunkGroup physically larger.

A Phase-1 validation build used:

```text
GROUPS_PER_REGION_EDGE = 9,000,000,000,000,000,000
REGIONS_PER_SECTOR_EDGE = 9,000,000,000,000,000,000
```

with the normal 16-block Chunk and 16-chunk ChunkGroup. Coordinate carry/borrow,
huge-Sector local movement, and mapped OpenSimplex tests passed. These large
radices are not enabled in the shipped build yet.

Noise phase and hierarchy hashes consume the address digits directly. They do
not need a number representing the total distance from origin, so later radix
expansion must not reintroduce global doubles or a giant integer multiplication.
