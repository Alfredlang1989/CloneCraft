# DEBUG HUD

Omnigrid has a lightweight Minecraft-style runtime diagnostic overlay rendered
through OgreNext's Overlay component.

## Toggle

- `F5` toggles the debug HUD on/off.
- The HUD starts hidden.
- `F` remains the flashlight toggle.
- `ESC` remains clean shutdown.

## Current fields

The top-left HUD currently shows:

- latest FPS and rolling-average FPS;
- latest frame time and rolling-average frame time;
- render-local camera XYZ relative to the current sticky render anchor;
- exact signed Sector X/Y/Z;
- local Region and Group X/Y/Z digits;
- local Chunk and Block X/Y/Z digits plus sub-block fractions;
- the complete sticky render anchor as Sector + Region + Group;
- configured ChunkGroup edge in blocks;
- dominant biome and normalized biome weights;
- runtime block id / stable block name at the camera voxel, or `<unloaded>`;
- hovered target block name/id/runtime id, hierarchical address, distance, collision/opacity flags, texture and tags;
- loaded chunk/group counts and streaming queue statistics;
- camera yaw/pitch;
- flashlight state.

## Coordinate interpretation

The camera does not store an absolute floating-point position. Its authoritative
position is:

```text
BlockAddress
  Sector            int64
  Region local       int64
  Group local        int64
  Chunk local        int64
  Block local        int64
fraction             float [0,1)
```

`Render XYZ` is the small position actually sent to Ogre relative to the sticky
`GroupAddress` render anchor. Terrain uses the same anchor, so the displayed
render frame describes GPU/renderer space directly.

The HUD intentionally does not reconstruct a global block/chunk integer for
presentation. This makes it useful as a regression detector for the hierarchical
coordinate architecture.

## Performance

The overlay is presentation-only. Biome formatting and string construction are
updated at 5 Hz (every 200 ms), not every rendered frame. Ogre FrameStats supply
the FPS/frame-time values.

## Rendering implementation

The HUD uses `OGRE-Next-Overlay` plus HLMS Unlit. Ogre's Overlay Font path needs
HLMS Unlit even though terrain itself uses HLMS PBS, so both HLMS implementations
are registered by `OgreRenderer`.

No font file is copied into Omnigrid. The renderer follows OgreNext's official
debug-resource layout first: `Media/2.0/scripts/materials/Common` plus
`Media/packs/DebugPack.zip`, and uses the `DebugFont` declared there. Like
OgreNext's own `resources2.cfg`, the `GLSL` shader-source subdirectory is
mounted as a separate non-recursive resource location: the `.program` scripts
reference their sources by flat name (`source Quad_vs.glsl`), and recursive
mounting would index them as `GLSL/Quad_vs.glsl`, which
`ResourceGroupManager::openResource` never resolves (symptom: a flood of
"High-level program ... not supported" + "Cannot locate resource ... .glsl"
messages at startup). If an installation does not ship that sample pack,
Omnigrid exposes an already installed system TTF through a dedicated Ogre
resource group and creates `Omnigrid/DebugFont` from it. It never installs or
copies a system font.

The visual style is deliberately simple: white diagnostic text plus an offset
black shadow, positioned at the top-left of the viewport.


## Crosshair hover selection (v18.2)

The centered crosshair and block selection are configured by `MODS/Default/ui.json`. The
crosshair texture path, pixel size and opacity are data-driven. The selected
block outline has data-driven reach, RGBA colour/transparency, geometric
thickness, expansion and depth testing.

Picking uses a voxel DDA over `BlockAddress`; only the short camera ray uses
floating point. Group/region/sector coordinates are never flattened into a
giant global `double`. The first loaded non-air block under the crosshair is
outlined and becomes the `Target:` shown by F5.

## Temporary v16 coordinate comparison

For the v16-to-v18 world-generation investigation the F5 HUD temporarily also
prints `Global block XYZ [TEMP v16 compare]`. This is a diagnostic-only mixed-radix
flattening of the player's current BlockAddress, plus `Global target XYZ [TEMP]`
for the block under the crosshair. Values are formatted from signed 128-bit
integers so the debug display does not truncate at int64 range. Production
streaming, rendering, movement, storage and worldgen continue to use hierarchical
addresses and never consume these comparison strings.
