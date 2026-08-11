# Hierarchical coordinates, Phase 2

Phase 2 is active. Persistent spatial identity is:

```text
Sector -> Region -> Section -> ChunkGroup -> Chunk -> Block
```

Every digit is `std::int64_t`. Sector is signed. All lower digits are canonical
non-negative local digits. The shipped logical super-radices are 9e18 for
Regions/Sector and Sections/Region; a Section contains 256 ChunkGroups per axis.
Physical Chunk/ChunkGroup dimensions remain 16 and 16.

## No flattening

Movement uses checked Euclidean carry/borrow through each digit. Code must not
construct an equivalent of `sector * sectorSize + ...`, because the Phase-2
range intentionally exceeds ordinary machine-wide flattened integer types.

## Worldgen

Mapped OpenSimplex folds hierarchy digits directly modulo its finite lattice
phase. The Section digit participates explicitly. Macro-warp hashing retains the
old Phase-1 hash representation whenever an address lies inside the complete old
Phase-1 representable range, preserving existing terrain identity there.

## Dynamic objects are separate

The hierarchy above is the block/worldgen/storage coordinate model. Dynamic
float objects do not inherit it. They live in `DynamicSpace` and cross the
boundary only through `WorldDynamicBridge`.

See `DYNAMIC_SPACE.md` and `PHASE2_VALIDATION.md`.
