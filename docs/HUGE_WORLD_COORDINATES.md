# Huge world coordinates

Phase 2 ships the hierarchy:

```text
Sector(signed int64)
 -> Region(9e18)
 -> Section(9e18)
 -> ChunkGroup(256)
 -> Chunk(16)
 -> Block(16)
```

This is an address space, not an allocation. Its approximate full linear span is
`9.79e61` blocks per axis. No subsystem attempts to allocate or flatten that
range.

Worldgen hashes/folds digits individually. Relative streaming and rendering use
bounded deltas. Crossing `INT64_MIN`/`INT64_MAX` at the outer Sector is rejected.

Dynamic float motion is intentionally independent. The default 65,536-block
DynamicSpace has a worst normal float32 spacing of 1/256 block at its half-edge,
while the render backend may use an even smaller local anchor.
