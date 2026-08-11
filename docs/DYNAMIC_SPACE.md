# DynamicSpace

## Purpose

`DynamicSpace` is the shared local coordinate frame for objects that need
floating-point motion:

- player/controller;
- NPCs and animals;
- vehicles and trains;
- dropped items/projectiles;
- future Jolt rigid bodies;
- other active dynamic entities.

It is not part of the persistent block address hierarchy.

## Hard dependency boundary

```text
world/*  <--- WorldDynamicBridge --->  spatial/dynamic + camera/Jolt
```

`worldgen`, chunk storage, streaming and meshing never depend on DynamicSpace.
The camera no longer depends on `world::WorldPosition`; it stores only a local
`spatial::dynamic::Position3f`.

`spatial::bridge::WorldDynamicBridge` is the explicit translation boundary. Its
anchor is an exact `BlockAddress` and rebase shifts are whole blocks.

## Default geometry

```text
edge       65,536 blocks
half-edge  32,768 blocks
range      [-32768,+32768)
scalar     float32
```

The edge is independent of `GROUPS_PER_SECTION_EDGE` even though both currently
produce the same 65,536-block scale. Either can change without changing the
other.

## Precision

Normal float spacing near key distances:

```text
~1,024 blocks   0.000122 block
~8,192 blocks   0.000977 block
~16,384 blocks  0.001953 block
~32,768 blocks  0.003906 block
```

At one metre per block the worst normal in-space spacing is about 3.906 mm.
Render space may still use a much smaller anchor for visual precision.

## Rebase

A rebase occurs only when a local coordinate leaves the half-edge range. The
rebase delta is an integer multiple of the DynamicSpace edge and therefore is
exactly representable as float for the supported edge range.

All active dynamic objects must receive the same local subtraction while the
bridge anchor receives the same world-space addition. This leaves every world
position unchanged.

Current Phase 1.5 wiring has only the free camera as a dynamic object. When Jolt
is integrated, its bodies and all other active dynamic entities must be shifted
as one transaction.

The actual Jolt simulation radius is independent. A 65k DynamicSpace can coexist
with a 500- or 1000-block active physics neighbourhood.
