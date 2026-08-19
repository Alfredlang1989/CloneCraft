# Reference source: M09 worldgen and M10 hierarchical fast travel

These topics have been promoted from recovery lanes R01/R02 to the explicit
main-spine contracts `docs/milestones/M09.md` and `docs/milestones/M10.md`.
This file is a source map only; it is not a second status or requirements
document. The commits are present both on preserved remote branches and in the
packaged `.git` history, but are not ancestors of the audited current `main`
source state.

## R01 Worldgen

Relevant commits:

```text
79a3e41  Rebalance biome geography with savanna and massif fields
4134b2b  Add macro oceans, inland lakes, and world-scale biome spacing
```

Desired semantics to recover:

- macro/planetary ocean distribution;
- actual inland lake candidates/basins;
- lake level, bottom, water bottom and shore mask;
- larger world-scale biome spacing;
- relevant savanna/massif geography;
- deterministic behavior and current biome-baseline tests.

Do not blindly cherry-pick.

The old branch paths use the former `data/` content layout. Current content is
under `MODS/Default`. Port algorithms/config/tests onto the current loaders and
contracts.

## R02 Hierarchical fast travel

Relevant commit:

```text
2f9a73b  Add hierarchical camera jumps for worldgen debugging
```

Desired controls:

```text
Arrow                      1 Block
Ctrl + Arrow               1 Chunk
Alt + Arrow                1 ChunkGroup
Shift + Arrow              1 Section
Ctrl + Alt + Arrow         1 Region
Ctrl + Alt + Shift + Arrow 1 Sector
```

Requirements:

- exact checked hierarchy arithmetic;
- preserve lower digits/fractional local camera state where appropriate;
- reject overflow;
- no astronomical float/double global position;
- rebase/update DynamicSpace, render anchor, streaming and selection coherently;
- modifier handling must not trigger normal Shift descend while performing a
  hierarchy jump.

Treat the old commit as reference implementation/behavior, not as authority over
the current ABI.
