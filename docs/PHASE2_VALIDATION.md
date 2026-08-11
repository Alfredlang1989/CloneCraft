# Phase-2 coordinate validation

Date: 2026-08-11

Phase 2 activates the default hierarchy:

```text
Sector(signed int64)
 -> Region(9e18)
 -> Section(9e18)
 -> ChunkGroup(256)
 -> Chunk(16)
 -> Block(16)
```

Phase 1.5 also separates dynamic float motion from block-world coordinates.

## Automated checks

Headless CMake/Ninja build completed successfully with the graphical application
disabled because the validation container does not provide SDL3/OgreNext.

`ctest` result: 15/15 tests passed.

The suite covers:

- carry through Block/Chunk/Group/Section/Region/Sector;
- negative Euclidean borrow;
- the 65,536-block Section seam;
- outer Sector overflow rejection;
- huge Region/Section/Sector addresses;
- mapped OpenSimplex continuity and huge-address variation;
- preservation of existing lowland river/worldgen regressions;
- 65,536-block DynamicSpace rebase behavior;
- world-position invariance across a DynamicSpace rebase;
- camera movement without a world-coordinate dependency.

`python3 tools/architecture_check.py --root .` result: PASS.

The dependency graph confirms:

```text
camera -> spatial.dynamic
spatial.bridge -> spatial.dynamic + world.coordinates
world.* -X-> spatial.dynamic/spatial.bridge
```

## Compatibility

The first Phase-2 test run exposed a changed macro-warp hash in negative/legacy
coordinates. This was corrected rather than accepting terrain drift.

Because one new Section is exactly 256 ChunkGroups, equal to one complete old
Phase-1 Sector in group units, the mapper can reconstruct the old Phase-1 hash
digits whenever the absolute Section index fits signed int64. Existing Phase-1
worldgen therefore retains its macro-warp identity across the whole old address
range. Phase-2-only space uses the new hierarchy hash.
