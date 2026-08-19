# OmniGrid Primus-Impetus roadmap

## Authority and workflow

This is the single authoritative milestone order/status document.
Architecture is defined only by `docs/ARCHITECTURE.md`.
Detailed acceptance requirements live in `docs/milestones/Mxx.md`.

A `/loop` handles exactly one milestone candidate. Internal implementation
steps are not review/status milestones. Do not stop for artificial "Round 1 / 2
/ 3" checkpoints. At the end of the selected milestone the independent reviewer
runs; after PASS the loop stops rather than automatically starting the next M.

Historical milestone numbers in old ADR prose do not define current ordering.

## Current main sequence

| Milestone | State | Dependency | Contract |
|---|---|---|---|
| M01 WorldState + hierarchical Sidecar foundation | ACCEPTED | baseline | implemented baseline |
| M02 Communication foundation + block interaction | ACCEPTED | M01 | implemented baseline |
| M03 Router + gameplay Lua + scheduler + two-block proof | REVIEW PENDING | M02 | `docs/milestones/M03.md` |
| M04 enTT hot-state projection | OPEN | accepted M03 | `docs/milestones/M04.md` |
| M05 RocksDB persistence | OPEN | M04 | `docs/milestones/M05.md` |
| M06 Embedded client/server | OPEN | M05 | `docs/milestones/M06.md` |
| M07 Construction foundation | OPEN | M06 | `docs/milestones/M07.md` |
| M08 Construction blueprints/jobs | OPEN | M07 | `docs/milestones/M08.md` |
| M09 Macro biomes, oceans and inland lakes | OPEN | M08 | `docs/milestones/M09.md` |
| M10 Hierarchical fast travel | OPEN | M09 (ordering only) | `docs/milestones/M10.md` |

`docs/ACTIVE_FINDINGS.md` contains the currently known M03 review blockers.
Once a milestone is externally accepted, this roadmap is the only status file
that needs to be advanced. Do not create another status ledger.

## Main dependency spine

```text
M01 state/Sidecars
   -> M02 communication
      -> M03 Lua + scheduling + integrated bus proof
         -> M04 hot ECS projection
            -> M05 durable persistence
               -> M06 transport/server authority
                  -> M07 bulk construction
                     -> M08 blueprints/jobs
                        -> M09 macro biomes + hydrology
                           -> M10 hierarchical fast travel
```

The order is architectural: later milestones must consume the earlier public
contracts instead of replacing them.

## Promoted recovery references

The two preserved recovery topics are now explicit end-of-spine milestones.
Their historical commits remain reference material, not alternate contracts:

- M09 ports useful R01 worldgen behavior onto the current data-driven content
  model.
- M10 ports useful R02 hierarchy-aware navigation behavior onto the current
  coordinate/DynamicSpace contracts.

Exact source links and fallback `git show` commands live in
`docs/RECOVERY_WORLDGEN_FASTTRAVEL.md`. Do not read that reference packet while
working on M04-M08; M09/M10 list the exact subset each run needs.

## Parallel lanes

These remain separate from the main M sequence unless an explicit dependency
requires work:

- P01 Render / Streaming / LOD / progressive materialization / residency
  (`docs/STREAMING_RESIDENCY.md`)
- P02 Vegetation
- P03 RmlUi
- P04 Audio
- P05 Celestial / astronomy
- P06 Fluids
- P07 Villages
- P08 Jolt physics

Binding cross-lane invariants are already in `docs/ARCHITECTURE.md`. Detailed
lane/domain docs are read only when a selected milestone touches them.

## Planning depth for M04-M10

M04-M10 are no longer placeholders. Their contracts define state ownership,
threading, forbidden shortcuts, integration boundaries and mandatory proofs.
The planner may refine implementation ordering from current code, but may not
weaken those contracts or invent an alternate architecture.
