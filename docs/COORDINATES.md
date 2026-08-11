# COORDINATES

CloneCraft Phase 2 uses two deliberately separate coordinate domains.

## Persistent block world

The block/worldgen hierarchy is exact and integer-only:

```text
Sector      signed int64 X/Y/Z
  Region    local 0 .. 8,999,999,999,999,999,999
    Section local 0 .. 8,999,999,999,999,999,999
      Group local 0 .. 255
        Chunk local 0 .. 15
          Block local 0 .. 15
```

Default radices:

```text
BLOCKS_PER_CHUNK_EDGE    = 16
CHUNKS_PER_GROUP_EDGE    = 16
GROUPS_PER_SECTION_EDGE  = 256
SECTIONS_PER_REGION_EDGE = 9e18
REGIONS_PER_SECTOR_EDGE  = 9e18
```

A Section is therefore exactly 65,536 blocks per axis. Section, Region and
Sector are logical address tiers, not dense allocated voxel containers.

No production code may flatten this hierarchy to one global XYZ integer or an
absolute double. Carry/borrow runs digit-by-digit and outer Sector overflow is
rejected rather than wrapped.

## DynamicSpace

Player/controller, future NPCs, vehicles, projectiles and Jolt bodies use a
separate local float coordinate frame. Default DynamicSpace edge is 65,536
blocks, range `[-32768,+32768)`. The equality with Section size is a policy
choice, not an architectural dependency.

`spatial::bridge::WorldDynamicBridge` is the only translation boundary between
DynamicSpace and the hierarchical block world. The camera itself has no world
coordinate dependency.

## Render space

Ogre keeps its own smaller sticky render anchor. Render rebases may therefore be
frequent and cheap without forcing a DynamicSpace/Jolt rebase.

See `DYNAMIC_SPACE.md`, `HIERARCHICAL_COORDINATES.md` and
`PHASE2_VALIDATION.md`.
