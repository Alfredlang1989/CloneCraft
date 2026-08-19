# HIERARCHICAL SIDECARS

Status: **implemented in M01-B, commit `a848de9` (#20).**

GitHub source: issue #20.

## Existing state

Current block-local storage:

```text
Chunk
 -> sidecar id
   -> localBlockIndex
     -> PropertyValue
```

This remains valid for block properties such as orientation.

## Required hierarchy-object scopes

```text
Sector
 -> Region
   -> Section
     -> ChunkGroup
       -> Chunk
         -> Block
```

Registered Sidecar/property target scopes:

```text
block
chunk
chunk_group
section
region
sector
```

Chunk metadata is keyed by `ChunkAddress`, never by a reserved local block slot.

## Canonical identity

Reuse existing coordinate digits/types.

Conceptual hierarchy keys:

```text
Block      = BlockAddress
Chunk      = ChunkAddress
ChunkGroup = GroupAddress
Section    = Sector + Region + Section
Region     = Sector + Region
Sector     = Sector
```

If explicit Section/Region address view types are needed, add thin canonical
types around the existing digits. Do not create a parallel coordinate model.

Never flatten into astronomical global XYZ integer/double.

## Sparse storage

For every scope:

```text
default value
 -> no explicit entry

non-default
 -> sparse entry

return to default / remove
 -> entry disappears
```

Untouched hierarchy nodes consume effectively no metadata storage.

Section/Region/Sector are logical coordinate tiers. They remain **unmaterialized**.
A metadata write at Region scope must not instantiate Regions, Sections,
ChunkGroups or Chunks.

## Registry

Sidecar metadata keeps:

- namespaced id;
- target scope;
- value type;
- default;
- optional bit width;
- storage strategy;
- persist;
- serialization version.

Scope mismatch is a validation error.

`core:orientation` is block-scoped.

## WorldState

Do not create six unrelated APIs/databases.

Introduce one scope-aware target concept reusable by M02 communication:

```text
WorldStateTarget =
  Block
| Chunk
| ChunkGroup
| Section
| Region
| Sector
```

Convenience overloads are fine, but one logical resolver/validation contract
must remain underneath.

Block properties retain prototype capability/default semantics.

Hierarchy-object properties initially use their registered definition/default.
No implicit Region->Section->Chunk inheritance in M01-B.

## Persistence boundary

Property delta identity must be able to carry:

```text
scope
canonical address
property id
value OR removal
```

`persist:false` is filtered for every scope.

RocksDB comes later.

## Sidecar Core is state, not policy

Sidecars may store facts/constraints such as a residency requirement, but the
generic Sidecar layer must not know:

- factories;
- claims/factions;
- stargates;
- renderer policy;
- streaming SLA;
- simulation algorithms;
- multi-owner pin lifecycle.

Consumers interpret the values.

If sparse metadata fits an existing hierarchy address, do not invent parallel
stores such as `PinnedChunkTable`, `FactoryChunkState` or `RegionFlags`.

## Required M01-B tests

For every scope:

1. default write allocates nothing;
2. non-default creates one entry;
3. get returns the entry;
4. reset/default removes it;
5. wrong scope is rejected;
6. two distinct canonical addresses do not collide.

Additionally:

- same local Section digits in different Region/Sector remain distinct;
- Chunk property and block property coexist;
- ChunkGroup property creates no Chunk;
- Section/Region/Sector property creates no lower-level container;
- deterministic enumeration;
- invalid value/bit width rejected;
- unknown property rejected;
- `persist:false` filtered;
- removal represented explicitly;
- metadata can exist independently of render residency.

## P01 relationship

Keep four axes separate:

1. materialization;
2. world/data residency;
3. simulation residency;
4. render residency/LOD.

`not rendered != not simulated`.

A simulation requirement may keep logical data resident while Ogre resources
are gone. Multiple independent hard-residency requirements must not collapse
to one fragile boolean. Owner/reason/refcount logic belongs to the consuming
residency service, not the Sidecar template.
