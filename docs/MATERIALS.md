# BLOCK MATERIALS

Block appearance is defined in `data/blocks.json`. Terrain rendering uses OgreNext HLMS PBS.
The C++ renderer translates registry values; it must not contain per-block colour/material tables.

## Visual source priority

Diffuse/base colour keeps the established fallback order:

1. `texture` if present and loadable;
2. otherwise `color`;
3. otherwise the renderer's magenta/black diagnostic texture.

A configured `color` therefore remains useful as the fallback for a missing file texture.

## PBR fields

Example:

```json
{
  "id": "example:polished_glass",
  "displayName": "Polished Glass",
  "solid": true,
  "opaque": false,
  "texture": "textures/blocks/glass.png",
  "color": "#A8D9FF",
  "normalMap": "textures/blocks/glass_n.png",
  "normalMapStrength": 0.75,
  "roughness": 0.08,
  "metalness": 0.0,
  "reflection": 0.12,
  "transparency": 0.35,
  "refraction": 0.08,
  "indexOfRefraction": 1.52,
  "reflectionMap": "textures/environment/workshop.dds",
  "receiveShadows": true,
  "castShadows": false
}
```

Fields:

| field | range/default | meaning |
| --- | --- | --- |
| `texture` | optional | diffuse/base-colour texture |
| `color` | optional | `#RRGGBB`, `#RRGGBBAA` or integer RGB(A) array; fallback when `texture` is unavailable |
| `normalMap` | optional | tangent-space normal map |
| `normalMapStrength` | `0..4`, default `1` | normal-map weight |
| `roughness` | `0.02..1`, default `0.85` | micro-surface roughness; low = glossy |
| `metalness` | `0..1`, default `0` | metallic workflow amount |
| `reflection` | `0..1`, default `0.04` | non-metal Fresnel/F0 reflectance; in metallic workflow used as specular strength |
| `reflectionMap` | optional | cubemap/environment reflection texture, e.g. cubemap DDS |
| `alphaMode` | `opaque` / `mask` / `blend`, default inferred for legacy data | `mask` uses alpha-test cutout; `blend` uses PBS transparency |
| `alphaCutoff` | `0..1`, default `0.5` | cutout threshold used by `alphaMode: mask` |
| `transparency` | `0..1`, default `0` | Clonecraft convention for `blend`: `0` opaque, `1` fully transparent |
| `refraction` | `0..1`, default `0` | requested screen-space refraction strength |
| `indexOfRefraction` | `1..3`, default `1.45` | optical IOR metadata / Fresnel basis |
| `receiveShadows` | bool, default `true` | material receives sun shadows |
| `castShadows` | bool, default `true` | geometry is placed in the shadow-casting chunk object |
| `emission` | integer `0..15` | prototype emissive material intensity |

If `alphaMode` is omitted, legacy `transparent=true`, `transparency > 0`, or `refraction > 0` infer `alphaMode: blend`. `alphaMode: mask` is deliberately non-opaque for voxel face culling but remains in the normal depth-writing render queue.

## Normal maps

The greedy mesher emits a normalized tangent for every vertex. Ogre ManualObjects upload position + normal + tangent + UV, so `normalMap` is a real tangent-space normal-map path rather than just a parsed JSON field.

Greedy UVs still span whole block counts (`0..N`) and use `TAM_WRAP`; because every block material owns/binds one diffuse and optional normal texture instead of one packed atlas, both diffuse and normal maps repeat per block across large greedy quads.

## Reflection

`reflection` controls the PBS specular/Fresnel response. Low-roughness materials such as water produce a strong sun highlight even without a reflection cubemap. `reflectionMap` optionally binds an Ogre PBS cubemap for explicit environment reflections.

## Refraction status

The registry and material layer support `refraction` and `indexOfRefraction`. OgreNext's true screen-space refractive mode requires a dedicated compositor scene pass with `use_refractions`, colour-copy and depth inputs. The current daylight compositor intentionally keeps refractive blocks in stable PBS transparency mode while storing/configuring refraction strength. This avoids pretending that setting one material flag is sufficient and avoids the rendering-order artifacts documented by Ogre's refraction sample.

A dedicated refractive compositor pass is therefore a separate renderer milestone. Until then, water/glass get PBS transparency, roughness, reflection, lighting and shadows, but not screen-space background displacement.


## Colour space / PBS albedo

Clonecraft treats JSON hex colours and diffuse texture files as sRGB authoring data.
The renderer converts JSON solid colours to linear RGB before passing them to
`HlmsPbsDatablock::setBackgroundDiffuse`; diffuse textures are loaded with
`PrefersLoadingFromFileAsSRGB`. The Ogre render window has `sRGB Gamma Conversion=Yes`
so PBS lighting stays linear internally and the final image is encoded back to sRGB.

Visual priority remains: `texture` -> `color` -> built-in diagnostic checker texture.
A plain JSON `color` no longer creates an asynchronous generated texture; it is a direct
PBS material constant. This avoids transient/dummy-texture residency affecting solid colours.

## Alpha mask / cutout vegetation

Foliage and cross-plane plants should use binary alpha cutout rather than conventional
alpha blending:

```json
{
  "alphaMode": "mask",
  "alphaCutoff": 0.5
}
```

The renderer maps this to OgreNext `HlmsDatablock::setAlphaTest` while leaving the
material in the normal depth-writing render path. The same alpha test is active for
shadow-caster shaders, so transparent texels do not cast solid box-shaped shadows.
Use `alphaMode: blend` only for genuinely transmissive materials such as water or glass.

## v16.5: textured terrain cubes

The default terrain blocks now exercise the same static file-texture path as vegetation.
`blocks.json` assigns PNG diffuse textures to stone, dirt, grass, forest grass, sand, gravel,
sandstone, snow, red sand and red sandstone. The existing JSON `color` remains as a fallback,
but texture has higher visual priority. No block-specific C++ material logic is used.

All test textures are static 32x32 RGBA PNG files under `data/textures/`.
