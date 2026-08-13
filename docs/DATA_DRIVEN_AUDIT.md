# Data-driven / AST audit

Audited baseline: **v18.3.8 natural river profile**. Cleanup result: **v18.3.9**, reviewed on 2026-08-10.
This document is the current-state companion to the historical review/audit files.

## Method

The headless build was configured with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, built,
and tested. Clang 17 was then used to produce filtered JSON AST dumps for the
content/config ownership boundary:

- `src/world/worldgen/WorldGen.cpp`
- `src/world/worldgen/WorldGenConfigLoader.cpp`
- `src/world/registry/RegistryLoader.cpp`
- `src/config/Settings.cpp`
- `src/ui/UiConfig.cpp`

The initial five-TU filtered AST set was about 60 MB; the two changed WorldGen/registry TUs were regenerated after the cleanup (about 31.7 MB). The audit inspected source-owned
`StringLiteral`, numeric literal, member-access and call nodes instead of relying
only on text grep. Renderer/app source was additionally reviewed directly because
the audit container does not have the target SDL3/OgreNext development setup.

Validation used during this audit:

- headless CMake/Ninja build: PASS;
- 15/15 CTest test executables: PASS after the cleanup (including the new biome-detail schema regression);
- `tools/architecture_check.py`: PASS;
- target `clang-tidy` could not be rerun in this container because no clang-tidy
  executable is installed; the normal target `./compile.sh` remains authoritative
  for the full renderer translation units.

## World/content ownership result

### Clean boundaries

`WorldGen.cpp` contains **no concrete `core:*` material ids** and no concrete
`river_*`, biome-mask, clay, sand or gravel field ids. It resolves fields,
passes, tags, masks, priorities and block ids from immutable registry/config data.
The only structural block id hard-coded by the engine is `core:air`, whose runtime
id 0 is an explicit architecture invariant.

The river added in v18.3.6-v18.3.8 is therefore content, not a C++ river system:
its route/profile/tunnel/sediment fields live in Lua; water, sand, clay, gravel,
dirt, Air-carve and cave-sealing rules live in `data/worldgen.json`.

`WorldGenConfigLoader` understands only generic operators and schema concepts:
2D/3D fields, conditions, `fill_below`, `surface_layer`, `surface`, `volume`,
replacement sets/tags, anchor sets and decoration operators. There is no river,
tree species, biome or material switch in that loader.

`RegistryLoader` owns strict JSON validation, not game-content decisions. Biome
terrain mask bindings and terrain parameters are registry data. Block rendering
shape/material properties and semantic tags remain registry data.

### Data leak found and fixed by this audit

Biome detail terrain was almost data-driven but not completely: `detailMultiplier`
was JSON-owned while the detail noise **scale 0.018** and **amplitude 3.0** were
hard-coded in `WorldGen::terrainProfileHeight()`.

The audit moves those two values to `BiomeTerrainDef` as `detailScale` and
`detailAmplitude`, parses/validates them in `RegistryLoader`, and writes them
explicitly into every shipped biome. The shipped values remain 0.018 and 3.0, so
this is an ownership cleanup rather than an intentional terrain retune.

### Stage-order follow-up (v18.4.0)

The pass runtime now exposes only the generic semantic stages `terrain` and
`addon`. Shipped JSON assigns every block-mutating pass explicitly. C++ enforces
the hard barrier `terrain -> addon -> decoration`; priorities are scoped to one
stage and therefore cannot accidentally move a river/cave operation across
vegetation.

The river/sugar-cane regression also removed a content coupling: `river_reeds`
no longer derives Y from `river_level + 2`. It is a normal postprocess-surface
decoration anchor gated by river fields, and sugar cane remains a generic column
decoration with data-defined support blocks. Source/AST checks contain no
`sugar_cane`, `river_reeds`, `river_mask` or `river_valley_mask` identifiers in
`src/world/worldgen`; those names remain content data only.


### Acceptable engine constants

Not every numeric literal is content. The AST still shows internal constants for
hash mixing/domain separation, integer-overflow guards, normalization of noise,
array/radix operations and small floating-point epsilon guards. Those define the
algorithm/ABI rather than a particular world or material and intentionally stay
in C++.

Default values in config structs are also retained as safe programmatic/test
fallbacks. Shipped runtime content explicitly overrides content-sensitive values
through JSON where required.

## Settings/UI ownership

Persistent user settings are data-backed through
`~/.config/Omnigrid/settings.json` (or `$XDG_CONFIG_HOME`). Current settings own:

- window width/height/fullscreen/resizable;
- chunk render distance and per-update commit budget;
- camera movement speed and mouse sensitivity;
- Ogre RenderSystem/plugin/log/config options;
- camera near/far clips and shadow far distance;
- Forward3D enable/grid/light budget/range.

`data/ui.json` owns crosshair texture/size/opacity and selected-block outline
reach/colour/thickness/expansion/depth-test.

## Remaining non-data-driven renderer tuning

The project is **not yet 100% data-driven in renderer tuning**. Direct source
review found these current Ogre constants in `OgreRenderer.cpp`:

- sky clear colour;
- upper/lower ambient colours and ambient direction/intensity;
- sun diffuse/specular colour, power, direction and shadow toggle;
- flashlight diffuse/specular colour, power, radius, cone angles, attenuation
  tail and shadow toggle;
- detailed PSSM/shadow-map setup where not already covered by `shadowFarDistance`;
- a few renderer-internal queue/resource/workspace ids.

The names/queue ids are engine implementation details. The lighting/sky/PSSM
values are user/render tuning and are the main remaining candidates for
`settings.json` (or a future shipped render-profile JSON). This audit does not
silently move Ogre-sensitive values because the graphical target cannot be
compiled in this container.

## Documentation drift found

The audit found stale current-state statements in `INDEX.plan`, `WORLDGEN.md`,
`REGISTRY.md`, `CODE_AUDIT.md`, `CODE_REVIEW_V10.md` and `DECISIONS.md`, mainly
claiming biome terrain/rivers/decorations were still future work. These files are
updated or explicitly marked historical in the accompanying cleanup.

## Current conclusion

The **world/content pipeline is cleanly data-driven after the detail-scale fix**:
materials, biome terrain profiles, river morphology/materials/sealing, caves,
plants and tree species are data/Lua concerns; C++ provides generic execution,
validation and deterministic merge primitives.

The remaining data-ownership debt is mostly **renderer tuning**, not worldgen.
