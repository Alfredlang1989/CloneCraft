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

## sidecars.json

Sparse/optional per-chunk sidecars (issue #3, section 5): a sidecar exists only
while at least one block in the chunk needs that data, and disappears again
when the last entry returns to its default. Mods declare sidecar *type
definitions/metadata* in `sidecars.json`; the optimized C++ storage may vary
per sidecar (section 5.1). The file is optional — a content root without
sidecar types is legal.

| Field | Type | Required | Default | Meaning |
|---|---|---:|---:|---|---|
| `id` | string | yes | - | namespaced sidecar id (`<namespace>:<name>`) |
| `displayName` | string | yes | - | user-facing name |
| `valueType` | string | no | `uint8` | `uint8` / `uint16` / `uint32` / `float` |
| `defaultValue` | unsigned or number | no | 0 | typed default: unsigned for integer types, number for `float` |
| `bitWidth` | unsigned | no | 0 | compact-encoding hint for integer types (1..32; 0 = full type width); metadata for storage/serialization (M09) only |
| `storage` | string | no | `sparse` | `sparse` / `dense` |
| `persist` | boolean | no | true | persistence policy |
| `serializationVersion` | unsigned | no | 1 | sidecar payload version (>= 1) |

Validation: duplicate ids, non-namespaced ids, unknown valueType/storage
strings, out-of-range bitWidth/serializationVersion and unknown fields are
rejected with source/entry context. `defaultValue` is validated against the
declared `valueType`: integer defaults must be unsigned and fit both the type
width and `bitWidth` (e.g. `uint8` + `bitWidth: 3` rejects `255`, max is 7);
`float` defaults must be a JSON number and may be fractional. `bitWidth` is
rejected for `float` value types.

### Runtime sidecar framework

`world::Sidecar<T>` (src/world/chunk/Sidecar.h) is the generic sparse
per-chunk store: empty until the first `set()`, entries removed when written
back to the default, deterministic ascending local-index iteration for later
serialization (M09). `set()` reports whether the stored state actually
changed, so callers can skip dirty/change notifications for no-ops; writes
outside the configured capacity (`Chunk::VOLUME` on the chunk path) are
rejected, closing the future deserialization trap (M09). `BlockOrientation`
(Up/Down/North/South/East/West; `bitWidth: 3` is a serialization/storage hint,
the pilot's physical storage is a sparse map entry) is the pilot type; the
chunk stores it (like every sidecar type) as a generic `PropertyValue` sidecar
keyed by the data-driven id `core:orientation`
(`world::CORE_ORIENTATION_SIDECAR`). No per-type members are added to Chunk
(M04 review constraint).

Lifecycle invariant (issue #3, section 5): sidecar state may exist only for
positions whose block actually needs it. Orientation writes to AIR blocks are
rejected, and replacing a block (including by AIR) or `assignBlocks` wholesale
content replacement clears the stale entries — no zombie sidecar state
survives a block change. The lazy-destruction invariant holds for every type:
once the last non-default entry returns to its default, the sidecar is dropped
again.

### Registry-driven resolver (M05)

M05 delivers the registry-driven resolver as the unified world state
(`src/world/state/WorldState.h`): game code calls `has()`/`get()`/`set()` by
*any* declared sidecar id — not just `core:orientation` — and never learns
whether a value comes from a stored sidecar entry or from the type's
data-driven default. `get()` falls back to `SidecarDef::defaultValue` (an
absent or unloaded chunk still answers with its declared default; only unknown
ids return `nullopt`), `has()` reports whether explicit stored state exists,
and `set()` rejects unknown ids, type-mismatched values and AIR positions and
never creates chunks. Writes of the declared default remove the stored entry
again. `WorldState` is also the single central block-mutation entry point
(`setBlock`) with granular change hooks (`what` = `"block"` or the property
id) and a `PersistenceSink` abstraction (M09 implements the RocksDB backend;
the reference `MemoryPersistenceSink` records dirty chunks and last-write-wins
deltas). ChunkManager keeps `setBlockOrientation`/`blockOrientation` as
convenience shims over the generic `core:orientation` sidecar — the shim and
the unified world state read and write the exact same storage.

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
