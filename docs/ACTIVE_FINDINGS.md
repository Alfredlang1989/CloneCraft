# Active review findings

This file is a small live queue, not a historical review log.
Only unresolved current milestone blockers belong here. Finished findings are
removed after external acceptance instead of accumulating forever.

Current scope: **M03**
Source: independent full-milestone review on 2026-08-19.
State: **OPEN**

Candidate repair evidence (awaiting independent acceptance):

- F1: proof semantics moved to shipped `gameplay.json`/Lua; production uses
  generic `GameplayContentRuntime`, property mutation and tint projection;
- F2: peer address uses typed `BlockTargetPayload`; non-Reply proof traffic is
  tested with empty `replyTo`;
- F3: every mesh vertex records the emitting voxel; regression covers all six
  cube faces;
- F4: bootstrap waits for generated chunks and uses declared remove/place bus
  commands without changing chunk materialization count;
- F5: Round-4 acceptance loads the shipped registry, manifest and Lua files;
- F6: generated shader-cache files are absent from the candidate diff.

These entries remain OPEN until the separate reviewer accepts the milestone, as
required by this file's lifecycle rule.

The initial M03 repair plan must address all already-known findings below. Future
harness reviewer passes are fail-fast and return only the first newly observed
blocking finding; they do not build another giant TODO list.

## F1 - Content-specific M03 proof semantics leaked into production C++

`src/world/scripting/TwoBlockProof.*` and Application integration contain
`test:*` action/content IDs and red/green proof semantics in production C++.
This violates the architecture contract that generic C++ owns mechanisms, not
content concepts.

Required closure: move proof/game semantics into data/Lua/registered generic
mechanisms. Production C++ may provide generic property/visual mechanisms but
must not know Block A/B or proof-specific content IDs/colors.

## F2 - `replyTo` is misused as block-coordinate payload

The permanent envelope contract defines `replyTo` as a logical reply address.
M03 currently encodes peer block coordinates such as `"130,51,128"` into that
field for the proof.

Required closure: peer/target data uses a proper typed transportable payload or
registered logical addressing mechanism. `replyTo` remains reply semantics.

## F3 - Positive-face tint owner lookup can select the neighbor block

The renderer derives a quad's owning block from `floor(firstVertex.xyz)`. On
+X/+Y/+Z faces the face plane is at `s+1`, so the lookup can resolve the
neighbor instead of the block that emitted the face.

Required closure: resolve owner block from face/quad semantics correctly for
all six directions and add a regression proof covering +/-X, +/-Y and +/-Z.

## F4 - Two-block proof can pre-materialize an empty chunk before worldgen

Application inserts A/B before normal streaming/worldgen has materialized the
chunk. The resulting non-empty chunk can then look "already loaded" to
streaming and skip normal terrain generation.

Required closure: insert proof content only through a path that preserves the
normal materialization/worldgen lifecycle. The proof must not create an
otherwise empty fake terrain chunk.

## F5 - Acceptance test embeds copies instead of testing shipped Lua/content

`tests/TestM03Round4.cpp` embeds its own Lua/JSON proof content. Shipped
`MODS/Default/scripts/block_a.lua` or `block_b.lua` can therefore break while
the acceptance test remains green.

Required closure: automated acceptance exercises the actual shipped content
files/path, or mechanically proves exact shared source identity without a
second hand-maintained copy.

## F6 - Unrelated tracked Ogre shader-cache changes pollute the candidate diff

Generated `*PixelShader_ps.glsl` / `*VertexShader_vs.glsl` cache changes are
unrelated to M03 and include host/runtime-specific output.

Required closure: remove unrelated generated shader-cache modifications from
the M03 candidate. Do not hide genuine source changes while cleaning the diff.
