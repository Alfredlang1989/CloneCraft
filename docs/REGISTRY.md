# REGISTRY

Content definitions live in JSON under the content root (`MODS/Default/`). Runtime numeric block ids are
compact implementation details; persistent/data-facing ids are namespaced
strings such as `core:stone`.

## blocks.json

| Field | Type | Required | Default | Meaning |
|---|---|---:|---|---|
| `id` | string | yes | - | namespaced id |
| `displayName` | string | yes | - | user-facing name |
| `solid` | boolean | no | false | gameplay/physics solidity |
| `transparent` | boolean | no | false | transparent rendering semantics |
| `opaque` | boolean | no | true | occludes neighbouring faces/light |
| `emission` | integer | no | 0 | 0..15 |
| `texture` | string | no | empty | image path relative to data root |
| `color` | string/array | no | absent | fallback albedo sent directly to PBS when no texture is used |

### Visual fallback order

The renderer resolves block visuals strictly in this order:

1. `texture` if present and loadable;
2. `color` if no usable texture exists;
3. built-in magenta/black diagnostic checker if neither is usable.

A definition may intentionally contain both a preferred texture and fallback
colour:

```json
{
  "id": "example:bricks",
  "displayName": "Bricks",
  "solid": true,
  "transparent": false,
  "opaque": true,
  "texture": "textures/blocks/bricks.png",
  "color": "#A05040"
}
```

If the file is missing or Ogre cannot load it, the colour is used instead of
turning the block invisible or crashing the renderer.

`color` accepts:

```json
"color": "#8A8A8C"
"color": "#3760D6B8"
"color": [138, 138, 140]
"color": [55, 96, 214, 184]
```

Channels must be integer 0..255. The old stone/dirt/grass/sand/water/gravel
colour table has been removed from C++; only the deliberate last-resort
magenta/black diagnostic pattern remains hardcoded.

## Runtime BlockId table

`core:air` is always runtime id 0 regardless of JSON ordering. Unknown content
ids now throw `RegistryError`; they no longer silently map to AIR. Callers that
really want a probe use `tryIndexOf()`.

## biomes.json

Current fields:

| Field | Type | Required | Default | Meaning |
|---|---|---:|---|---|
| `id` | string | yes | - | namespaced biome id |
| `displayName` | string | yes | - | user-facing name |
| `surfaceBlock` | string | yes | - | descriptive/compatibility surface block reference |
| `fillerBlock` | string | no | `surfaceBlock` | descriptive/compatibility filler reference |
| `resourceId` | string | no | empty | legacy resource metadata reference |
| `temperature` | number | no | 0.5 | biome climate target metadata |
| `rainfall` | number | no | 0.5 | biome climate target metadata |
| `continentalness` | number | no | 0.5 | oceanic-to-continental metadata |
| `weight` | number | no | 1.0 | registry selection/weight metadata |
| `terrainMaskField` | string | no | empty/fallback biome | 2D worldgen field that weights this terrain profile |
| `terrain` | object | no | neutral profile | active terrain-shape parameters |

`temperature`, `rainfall` and `continentalness` must be within 0..1. A non-empty
`resourceId` must resolve after resources are loaded. The active field/pass worldgen
uses `terrainMaskField` plus the `terrain` object to blend surface height.

`terrain` supports:

| Field | Default | Meaning |
|---|---:|---|
| `heightOffset` | 0.0 | vertical offset added to the lowland base |
| `heightMultiplier` | 1.0 | multiplier for the shared lowland relief |
| `detailAmplitude` | 3.0 | amplitude of biome-local detail noise |
| `detailScale` | 0.018 | spatial frequency/scale of biome-local detail noise |
| `detailMultiplier` | 1.0 | additional biome tuning multiplier for detail |
| `ridgeAmplitude` | 0.0 | maximum ridged uplift |
| `ridgeScale` | 0.003 | ridged-noise scale |
| `ridgeSharpness` | 3.0 | ridge profile exponent |
| `islandAmplitude` | 0.0 | optional sparse island uplift |
| `islandScale` | 0.0075 | island-noise scale |
| `islandThreshold` | 0.78 | island activation threshold |
| `islandSharpness` | 2.0 | island profile exponent |

Scales satisfy `0 < scale <= 1`; amplitudes/multipliers are non-negative. The
shipped data writes detail scale/amplitude explicitly so terrain tuning does not
depend on hidden C++ constants.

`surfaceBlock`/`fillerBlock` remain registry metadata and valid block references, but
the active material graph places surfaces through `MODS/Default/worldgen.json` passes rather
than switching on biome ids in C++.

## resources.json

| Field | Type | Required | Default |
|---|---|---:|---|
| `id` | string | yes | - |
| `displayName` | string | yes | - |
| `blockId` | string | yes | - |
| `weight` | number | no | 1.0 |
| `chance` | number | no | 1.0 |
| `minY` | integer | no | 1 |
| `maxY` | integer | no | 64 |

`weight` is the relative selection weight in the global resource pool.
`chance` is the 0..1 probability that the selected per-column attempt is
placed. A biome `resourceId` bypasses global weighted selection and selects the
referenced resource explicitly.

## Validation

Unknown fields, duplicates, wrong types/ranges and invalid cross-references fail
with a message that includes source/entry context. JSON syntax is validated as
part of the repair checks.

## prototypes.json

Prototypes are the *logical* identity layer on top of physical block ids:
a voxel stores a compact block index, while gameplay refers to a stable
namespaced prototype id such as `default:cactus`. The file is optional — a
content root without prototypes is legal.

| Field | Type | Required | Default | Meaning |
|---|---:|---:|---|---|
| `id` | string | yes | - | namespaced prototype id (`<namespace>:<name>`, both non-empty) |
| `displayName` | string | yes | - | user-facing name |
| `blockId` | string | yes | - | linked physical block; must exist in `blocks.json` |
| `capabilities` | array of string | no | empty | declared capabilities/slots (pure declarations until the signal/slot layer) |

Validation: duplicate ids, non-namespaced ids, unknown `blockId` references,
duplicate capabilities and unknown fields are rejected with source/entry context.

### Runtime prototype handles

`PrototypeIdTable` maps prototype ids to stable 32-bit handles (FNV-1a hash of
the id). Handles never depend on load or insertion order, so the same content
loaded in different orders always produces the same handle. Hash collisions
are detected and rejected at construction time; ids stay persistent strings,
so a rejected collision is a hard error, never silent corruption.

Scaling note: FNV-1a 32-bit collides with probability ~1.2% at 10 000 ids and
~25% at 50 000 ids. That is acceptable for mod sets of the planned scale, and
the deterministic rejection keeps it a *load-time configuration error* rather
than a data hazard; if a very large mod ecosystem ever materialises, the
handle width can grow to 64-bit without changing the stable-id contract.

### Block-to-prototype bridge

The block-to-prototype mapping is strictly 1:1: every `blockId` is claimed by
at most one prototype (duplicate claims are rejected at load time), so logical
identity never depends on registry order. `world::prototypeForBlock( registry,
blockId )` returns the owning prototype or `nullptr`; most blocks are pure
scenery and are not referenced by any prototype. If sharing a block between
prototypes ever becomes necessary, identity must move into per-block state
(sidecars/ECS, M04/M05) - not into this bridge. `world::WorldObjectRef`
(position + prototype id) is the M03 foundation of the unified world-state API
(M05).

## Block semantic tags

Blocks may define a `tags` array. Tags are renderer-independent semantic labels
used by the data-driven worldgen merger, for example `terrain:rock`,
`terrain:carvable`, `terrain:soil` and `ore:replaceable`. Worldgen passes may
use `replaceTags` instead of enumerating concrete block IDs, so mod-defined
blocks can participate in geology without a C++ change.
