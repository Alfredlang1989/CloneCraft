# Worldgen pipeline audit — v18.4.0

## Result

The production mutation order is now explicit and enforced by the runtime:

1. `terrain`
2. `addon`
3. `decoration`

Numeric priorities are scoped to their stage. A low-priority addon cannot run
before a high-priority terrain pass, and decorations cannot be merged before
terrain/addons are finished.

## Shipped stage ownership

`terrain` owns base geology and surface construction such as base stone,
topsoil, biome surface material, desert/badlands strata and mountain caps.

`addon` owns features that modify completed terrain: deep dirt/gravel pockets,
caves, open river carving, mountain river tunnels, river cave sealing, bank/bed
sediment and water. Future ore/resource replacement belongs here as well.

`decoration` owns plants and structures. Tall grass, flowers, sugar cane, oak
and birch never participate in terrain/addon merge ordering.

## Sugar cane regression

The missing sugar cane was not caused by decoration blocks being merged before
the river. The block merge already happened later. The bug was its anchor:
`river_reeds` used a fixed `river_level + 2` field height and required sand under
that position. After the natural bank profile moved most inner banks to Y=0/Y=1,
that fixed anchor commonly floated above the bank and failed its support check.

`river_reeds` now uses `surfaceMode: "postprocess"` on `surface_height`. River
fields only gate eligible X/Z positions. The anchor then snaps to the final
surviving terrain+addon bank surface and the generic sugar-cane column validates
its data-defined support block.

A seed-1337 regression scan around the known lowland river finds sugar-cane
blocks after the fix.

## Data-driven boundary

The stage enum is generic (`terrain` / `addon`); it contains no geology names.
The shipped `worldgen.json` explicitly declares the stage of every mutating
pass. River field names, sugar-cane identity, support blocks, bank eligibility,
column height/chance and all priorities remain data.

A source/AST check of `src/world/worldgen` finds no concrete `core:sugar_cane`,
`river_reeds`, `river_mask` or `river_valley_mask` identifiers in the C++
worldgen runtime.

The Clang AST for `WorldGen::generateChunkIds` contains the stage applications
in source order: `terrainMergeOrder`, then `addonMergeOrder`, followed by the
separate `decorationMergeOrder` phase.

## Validation

- headless build: PASS
- CTest: 15/15 PASS
- architecture checker: PASS
- JSON parse validation: PASS
- modified worldgen/config/test translation units: Clang `-Wall -Wextra -Wpedantic -Werror` PASS
- focused Clang AST dump: PASS
