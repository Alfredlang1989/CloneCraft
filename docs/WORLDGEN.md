# World generation runtime

Clonecraft world generation is data-driven. `WorldGen` is an execution runtime;
it does not contain material rules such as "stone below the heightmap", "dirt at
the surface" or "caves remove terrain".

The active pipeline is:

```text
worldgen.json
    |
    +--> Lua scalar fields (2D or 3D), sampled in parallel
    |       surface_height(x,z)
    |       dirt_depth(x,z)
    |       deep_dirt(x,y,z)
    |       gravel(x,y,z)
    |       caves(x,y,z)
    |       desert/forest/badlands/alpine/high-mountain/snow masks
    |       desert-high-mountain mask
    |       river_mask, river_level
    |       river valley/tunnel route masks + 2D tunnel ceilings
    |       continuous bank/bed/water-bottom/sediment profile fields
    |
    +--> generic passes, proposal arrays built in parallel
            fill_below | surface_layer | surface | volume
                |
                v
         BlockProposal arrays
         {localIndex, blockId, priority, passOrder}
                |
                v
       stage 1: terrain merge
       priority -> JSON pass order
       + replaceBlocks / replaceTags
                |
                v
       stage 2: addon merge
       caves / rivers / tunnels / sealing / sediment / future modifiers
       priority -> JSON pass order
                |
                v
       immutable decoration anchor sets
       (postprocess anchors resolve the final terrain+addon surface)
       (surface/density/mask predicates)
                |
                v
       decoration passes in parallel
       scatter | column | structure
                |
                v
       deterministic decoration merge
                |
                v
           final dense chunk
```

## Why fields and passes are separate

A material can own its own independent noise without having to own the geometry
that bounds it. For example, topsoil uses the shared `surface_height` field and
an independent `dirt_depth` field. Underground dirt, gravel and future ores use
independent 3D fields. No material pass reads the output of another material
pass while it is being generated.

This is what makes the expensive part parallel: every field produces a dense
scalar array independently, and every pass turns those arrays into an
independent proposal array. Only the final merge is ordered.

## Lua fields

A field entry in `data/worldgen.json` names a Lua script, dimensionality and an
optional salt:

```json
{
  "id": "dirt_depth",
  "dimension": "2d",
  "script": "worldgen/dirt_depth.lua",
  "function": "sample",
  "salt": 2000
}
```

World scalar-field scripts export a function receiving only the derived seed:

```lua
function sample(seed)
    return noise2(0.0048, 101)
end
```

World X/Y/Z never enter Lua as a flattened integer or floating-point number.
The native helpers keep the v16 OpenSimplex-style scale API:

```lua
noise2(scale, salt [, offsetX, offsetZ])
noise3(scaleX, scaleY, scaleZ, salt [, offsetX, offsetY, offsetZ])
```

`scale` values are ordinary small doubles just as in v16 field scripts. The C++
mapper combines each scale with the current hierarchical `BlockAddress` while
preparing the OpenSimplex transformed-lattice phase; it never constructs a
world-wide floating-point coordinate. The inner OpenSimplex interpolation then
sees only the mapped lattice phase plus small group-local coordinates. This
preserves the v16 noise morphology while keeping precision independent of the
distance from the origin. Structure scripts are separate: they may receive
bounded local `dx/dy/dz` around a decoration anchor because those values are not
world coordinates.

Worldgen Lua is intentionally a numeric sandbox. `io`, `os`, `package`,
`debug`, `dofile`, `loadfile`, `require`, `math.random` and `math.randomseed`
are unavailable. Scripts should be pure scalar-field definitions.

Each worker thread owns a private Lua VM for each field and reuses it across
chunks. Scripts and mapped-noise state are therefore not recreated for every
streamed chunk. Separate fields still run concurrently without sharing Lua
state. WorldGen also keeps its worker threads alive instead of creating a new
group of `std::jthread`s per chunk.

Mapped OpenSimplex is prepared around the current ChunkGroup instead of
flattening a world coordinate for each sample. Entering a group folds the full
hierarchical address into the transformed 2048-cell OpenSimplex lattice phase.
Samples inside the group then use only local block coordinates in the range
0..255 plus that prepared phase. The v16 OpenSimplex2S interpolation and gradient
selection remain authoritative; the mapper changes only how a huge address
reaches that lattice. A small per-call cache retains transformed coefficients and
group phases for the scale/salt combinations used by Lua fields.

A separate low-pass macro warp is derived from the full hierarchical GroupAddress.
Its neighbouring macro cells share control points and use quintic interpolation,
so it is continuous at group/macro-cell borders. The default cell width is 32
ChunkGroups (8192 blocks at the current radix) with a horizontal amplitude of
64 blocks. This slow warp breaks the simple long-distance OpenSimplex wallpaper
repeat while leaving local v16-style morphology largely intact. It can be
disabled for A/B validation of the mapper itself.

Three-dimensional fields are divided into independent X slices before entering
the persistent worker pool. A default chunk with three 3D fields therefore
exposes 48 jobs instead of only three field-sized jobs, keeping wider CPUs useful
during the most expensive sampling phase. Within each slice the loop order
reuses the prepared horizontal mapping for all 16 Y samples of a column.

## Biome terrain blending

`surface_height.lua` is intentionally only the lowland macro relief. It stays
near the river reference level instead of embedding a global mountain pedestal.
After all 2D fields are sampled, `WorldGen` blends the terrain profiles from
`data/biomes.json` into that surface.

A biome may name a `terrainMaskField`, for example `high_mountains_mask`. The
mask is a continuous 0..1 weight. The unmasked biome is the fallback profile
(`core:plains` in the default data). Overlapping explicit masks are normalized,
which keeps biome transitions continuous instead of snapping columns between
height formulas.

The terrain profile fields are now live runtime inputs:

- `heightOffset` shifts the biome above/below the lowland reference;
- `heightMultiplier` scales the base relief;
- `detailAmplitude`, `detailScale` and `detailMultiplier` control the additional fine terrain noise;
- `ridgeAmplitude`, `ridgeScale`, `ridgeSharpness` build biome-specific ridges;
- the island parameters analogously add sparse island uplift when configured.

This keeps ordinary plains close to Y=0 while allowing rolling hills, alpine
terrain and high mountains to gain height explicitly from their biome data.
The same adjusted `surface_height` is used by terrain passes, decoration anchors,
postprocess surface queries and `WorldGen::surfaceHeight()`.

## Generic pass types

### `fill_below`

For every X/Z column, proposes the configured block at every chunk voxel where
`worldY <= floor(field(x,z))`.

The default stone pass is only data:

```json
{
  "id": "base_stone",
  "type": "fill_below",
  "stage": "terrain",
  "block": "core:stone",
  "priority": 0,
  "field": "surface_height"
}
```

### `surface_layer`

Proposes a layer ending at a 2D surface field. Thickness may be a constant or a
second 2D field. `surfaceOffset` shifts the top of the layer vertically, which
lets data stack strata without material-specific C++ (for example sand at
offset 0 and sandstone at offset -4).

Alternatively a layer may specify `bottomField` plus an optional `bottomOffset`.
In that mode the proposal spans the inclusive range from the sampled top down to
the sampled bottom and ignores thickness. The open river valley uses this with
the real biome-adjusted `surface_height` as the top and the continuous
`river_valley_floor` profile as the lower boundary. The same operator fills water
from `river_level` down to the varying `river_water_bottom`, which gives the
channel a U/V-shaped bed instead of a flat |_| cross-section.

The default topsoil pass uses `surface_height` plus the independent
`dirt_depth` noise field.

### `surface`

Proposes exactly one voxel at `floor(field(x,z))`. The default grass pass uses
this operator and may only replace dirt. In the base terrain stage this is the
geometric air-contact surface because no terrain fill exists above the height
field. More complex neighbour-sensitive surface post-processing belongs to a
later post-process stage.

### `volume`

Samples a 3D field and emits a proposal when its scalar comparison succeeds.
Supported comparisons are `always`, `gt`, `gte`, `lt`, `lte` and `between`.
Deep soil, gravel, ores and cave subtraction all fit this operator.

A cave is not special in C++:

```json
{
  "id": "caves",
  "type": "volume",
  "stage": "addon",
  "block": "core:air",
  "priority": 100,
  "field": "caves",
  "condition": { "op": "lt", "value": 0.165 },
  "replaceTags": ["terrain:carvable"]
}
```

The default cave field deliberately uses lower spatial frequencies and a wider
near-zero intersection than v13. The result is thick, walkable spaghetti tunnels
rather than one-block cracks; regression data includes a stable 3x3x3 clear cave
volume.

It is simply a high-priority proposal for Air.

## Optional 2D masks

Any pass may reference a `maskField` plus `maskCondition`. The mask is evaluated
once per X/Z column before that pass emits proposals. This is how the default
desert and river systems remain entirely data-driven.

Example:

```json
{
  "id": "desert_sand",
  "type": "surface_layer",
  "stage": "terrain",
  "block": "core:sand",
  "surfaceField": "surface_height",
  "thickness": 4,
  "maskField": "desert_mask",
  "maskCondition": { "op": "gt", "value": 0.30 },
  "priority": 50
}
```

`condition` remains the source-field comparison for 3D `volume` passes;
`maskCondition` is independent and always applies to the optional 2D mask.

## Replacement rules and block tags

`blocks.json` may assign semantic tags to blocks:

```json
{
  "id": "core:stone",
  "tags": ["terrain:rock", "terrain:carvable", "ore:replaceable"]
}
```

Passes can limit what they overwrite with `replaceBlocks` and/or `replaceTags`.
This keeps material semantics out of C++.

Examples:

- an ore pass can replace `ore:replaceable`;
- gravel can replace `terrain:rock` but not topsoil;
- caves can replace `terrain:carvable` but not a future protected block;
- a grass pass can replace only `core:dirt`.

If both replacement lists are empty, the pass may overwrite any current block.
If either list is present, a current block must match at least one listed block
ID or tag.

## Stages, priority and determinism

Passes never write directly into the chunk while running in parallel. They
produce proposal arrays. Every pass declares a semantic mutation stage:

- `stage: "terrain"` constructs the initial land: base stone, soil, biome caps,
  desert strata and similar material layers;
- `stage: "addon"` mutates the completed terrain: deep dirt/gravel pockets, caves,
  rivers, tunnels, river sealing/sediment, ores and future block-moving geology features.

The runtime enforces the barrier **terrain -> addon -> decoration**. Numeric
priority can never jump across that barrier. Inside each pass stage the merge
sorts by:

1. ascending `priority`;
2. JSON declaration order as the deterministic tie-breaker.

Later proposals inside a stage see the result of earlier proposals and must
still satisfy their replacement rule. Addons always see completed terrain, and
decoration always sees completed terrain plus addons. Thread scheduling is never
a tie-breaker. A given world seed, data set and chunk coordinate therefore
produces the same chunk independent of the configured worker count.

`workerThreads: 0` selects an automatic worker count. The runtime reserves one
logical CPU for the real-time side when more than one CPU is available, keeps a
minimum of one worker, and never creates more workers than the current job set
can use.

## Decoration stage: plants and structures

Vegetation runs only after both block-mutating stages have completed. Base terrain
is merged first, addons such as caves/rivers/tunnels/sealing second, and only then
does decoration modify the chunk. It therefore decorates the finished geological
world instead of influencing it.
The stage is still data-driven and has two layers: immutable `anchorSets` choose
stable world positions; independent `decorations` turn those positions into
block proposals.

An anchor set contains only placement data, for example a surface field, surface
mode, optional density field, grid spacing, chance, salt and 2D field predicates.
`surfaceMode: "field"` uses the field Y directly for genuinely fixed-level
decorations. River-bank vegetation should normally use `postprocess` so it follows
the surviving bank after all addons. `surfaceMode: "postprocess"` resolves the final staged terrain+addon graph at
the anchor column and snaps onto the surviving top block, so caves, river cuts
and other subtractive postprocessing cannot leave vegetation floating:

```json
{
  "id": "forest_trees",
  "surfaceField": "surface_height",
  "surfaceMode": "postprocess",
  "maxSurfaceDrop": 1024,
  "densityField": "forest_mask",
  "spacing": 9,
  "chance": 0.92,
  "salt": 10003,
  "conditions": [
    { "field": "forest_mask", "op": "gt", "value": 0.18 },
    { "field": "river_mask", "op": "gt", "value": 0.07 }
  ]
}
```

Each grid cell can produce at most one jittered candidate. Candidate position,
chance roll, structure seed and `variant` are hashes of world seed + anchor-set
salt + integer cell coordinate. Anchor generation therefore does not depend on
chunk load order or thread scheduling.

### Decoration pass types

`scatter` places one block at an anchor. Tall grass and individual flowers use
this pass.

`column` places a deterministic vertical run with `minHeight`/`maxHeight`.
Sugar cane uses this pass, but it is not owned by the river addon. Its shipped
`river_reeds` anchor is an ordinary `surfaceMode: "postprocess"` decoration anchor:
river fields only decide where a candidate is eligible, then the anchor snaps to the
actual surviving river-bank surface and the column validates its support block. This
keeps vegetation downstream of all dirt-moving river/cave work.

`structure` evaluates a bounded Lua structure function around every relevant
anchor. The Lua function returns `0` for no block or `1..N` to select an entry
from the pass' JSON `palette`. C++ knows only bounded structure geometry and
palette indices, never tree species or branch rules.

The shipped oak is deliberately split into two ordinary structure passes:

```text
forest_trees anchors (immutable)
        |                 |
        v                 v
    oak_wood           oak_leaves
      Lua                 Lua
        \                 /
         independent jobs
                |
                v
       deterministic merge
```

Both Lua functions reconstruct the same trunk/branch graph from the same anchor
seed. Leaves therefore do not need to wait for the wood thread and no mutable
"tree is ready" state crosses workers. Wood has the higher merge priority, so
logs deterministically replace overlapping leaves. Birch uses the same scheme.

Structure bounds are also the cross-chunk contract. A chunk scans anchor cells
far enough outside its own X/Z footprint for any declared structure bounds that
could reach it. Thus a tree is not cut at a chunk boundary, and a high-neighbour
crown can populate a sky chunk even when that chunk's own terrain surface is far
below it. The sky early-out uses the actual immutable anchors as a conservative
reach test before discarding a chunk.

### Data-driven plant rendering

`blocks.json` may set `"renderShape": "cross"`. The mesh builder treats that as
a generic crossed-plane shape and emits two diagonal texture planes with both
windings. Grass, flowers and sugar cane are ordinary blocks using this shape;
their identity and PNG path remain registry data. The shipped plant/wood/leaf
textures are static files under `data/textures/`, not images synthesized by C++
at runtime.

Current shipped decoration data contains tall grass, dandelions, poppies, river
sugar cane, procedural oak and procedural birch. Adding another species is a
data/Lua operation unless it requires a genuinely new generic decoration
primitive.

## Default terrain/geology

The current default data set demonstrates the architecture with large-scale
biomes, layered deserts, fixed-level rivers and thick caves. The first material
steps below are `terrain`; cave/river work is `addon`; vegetation is not part of
either list and runs afterward as decoration:

1. `surface_height.lua` creates only lowland macro relief near the Y=0 river reference.
   `WorldGen` then blends the active biome terrain profiles from `biomes.json`; explicit
   rolling/alpine/high-mountain ridge amplitudes may still produce terrain hundreds of
   blocks high and are intentionally not capped by a legacy world height;
2. `base_stone` fills everything below the height field with stone;
3. `deep_dirt.lua` creates independent underground soil pockets;
4. `dirt_depth.lua` controls variable topsoil thickness;
5. climate masks select active surface biomes. The shipped data currently
   includes Plains, Temperate Forest, Rolling Hills, Desert, Badlands, Alpine
   Highlands, High Mountains and Desert High Mountains. Forest uses
   `core:forest_grass`, Badlands use red sand/red sandstone, cold High Mountains
   may receive a two-block snow cap, while hot/dry massif cores override that
   snow with a two-block sand cap and fourteen blocks of sandstone;
6. `gravel.lua` creates independent 3D gravel pockets;
7. lowland deserts still produce four blocks of sand, seven blocks of sandstone
   and normal stone/geology below;
8. `river_mask.lua` remains the v16 warped 2D OpenSimplex zero-contour and
   the reference water surface remains Y=0. `river_profile.lua` derives a
   continuous U/V channel from distance to that contour: the thalweg is several
   blocks deep, the bed rises toward each shore, and the first bank blocks are
   normally at Y=0 before the bank gradually climbs to Y=1/Y=2;
9. one open-valley Air pass carves from the real biome-adjusted surface down to
   `river_valley_floor`. The profile rises continuously toward the outside of
   the valley, replacing the previous five hard shelves and avoiding a |_| canal
   silhouette. It never carves below its sampled lower boundary;
10. after cave subtraction, `river_substrate` and `river_bank_foundation` build
    finite dirt plugs beneath the channel and inner banks and are explicitly
    allowed to replace Air. Broad 2D sediment noise then overwrites the upper
    bed/foundation with patches of sand, clay, gravel or exposed dirt. Water is
    filled only above the sealed `river_water_bottom`, so a cave immediately
    below the river does not punch a hole through the bed;
11. only real mountain massifs enable the four nested tunnel ceilings. The route
    uses the same continental/rugged/crown macro signal as the high-mountain
    masks instead of reconstructing the obsolete surface-height formula. Tunnel
    Air is one-sided above the river (Y=1..10-50); there is no tunnel floor-space
    Air pass below Y=0;
12. `caves.lua` retains the v16 warped OpenSimplex formula and the cave pass
    replaces carveable terrain with Air.

`biomes.json` is active input for terrain shape: `terrainMaskField` binds each explicit
biome profile to a Lua mask and the terrain object owns height/detail/ridge/island
parameters. `surfaceBlock`/`fillerBlock` remain descriptive/compatibility metadata;
active material placement is still expressed by generic passes in `worldgen.json`.
No material, river or biome name is hard-coded in WorldGen C++.

The unused pre-field `TerrainSystem`, `RiverSystem`, `CaveSystem` and their legacy
feature configuration were removed because they still exposed global XYZ APIs.
`NoiseSource` and the vendored OpenSimplex2S implementation remain intentionally:
they are the v16-quality inner noise engine used by `MappedOpenSimplexNoise`.
There is no legacy production coordinate path that can silently flatten a chunk
back into world-wide XYZ.

Vegetation and structures remain the later decoration stage described above.

## Extending the geology

Adding an ore does not require a C++ worldgen change. Add a 3D Lua field and a
`volume` pass, for example:

```json
{
  "id": "iron_veins",
  "type": "volume",
  "stage": "addon",
  "block": "core:iron_ore",
  "priority": 50,
  "field": "iron",
  "condition": { "op": "gt", "value": 0.79 },
  "replaceTags": ["ore:replaceable"]
}
```

The same mechanism can add clay, granite, limestone, salt, deep soil, basalt,
ore families or entirely mod-defined geology.

## Streaming-oriented evaluation

2D fields are sampled before 3D fields. If the complete chunk lies above every
2D terrain/addon pass and no 3D volume pass is allowed to create blocks from Air,
WorldGen returns an all-Air chunk immediately. This is especially important
while flying, because upper chunks no longer evaluate cave/geology Lua for all
4096 voxels.

Chunk streaming itself is asynchronous: a background coordinator generates
chunks while the main thread commits only a bounded number per frame. Renderer
meshing/upload work is budgeted as well, so crossing a chunk boundary does not
force an entire incoming slab into one frame.

## Numeric world limits

Spatial identity is hierarchical rather than a flattened world coordinate. The
current fixed hierarchy is:

```text
Sector      signed int64
  Region    int64 0..15
    Group   int64 0..15
      Chunk int64 0..15
        Block int64 0..15
```

All discrete digits are `std::int64_t`. Carry/borrow keeps lower digits
canonical, and top-level overflow is reported instead of wrapping. At the
current radix this gives 65,536 blocks per Sector and an enormous signed
Sector range without ever composing a global block/chunk XYZ.

Noise receives the hierarchical address only while preparing an integer mapping
frame. Interpolation operates on small local fractions, so distance from the
origin does not consume floating-point precision. Domain-warp offsets remain
small local doubles and are normalized through the same hierarchy when they
cross a local boundary.
