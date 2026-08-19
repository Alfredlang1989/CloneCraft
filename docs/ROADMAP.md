# OmniGrid Primus-Impetus roadmap

## Authority and workflow

This is the single authoritative milestone order/status document.
Architecture is defined only by `docs/ARCHITECTURE.md`.
Detailed acceptance requirements live in `docs/milestones/Mxx.md`.

A `/loop` handles exactly one milestone candidate. Internal implementation
steps are not review/status milestones. Do not stop for artificial "Round 1 / 2
/ 3" checkpoints. The loop plans, builds and independently reviews that one
candidate. On dual PASS it marks exactly that milestone `ACCEPTED`, creates the
terminal backup and stops. The next `/loop` invocation selects the next eligible
milestone; the current invocation never starts it.

The controlled lifecycle is `OPEN -> REVIEW PENDING -> ACCEPTED`. The primary
orchestrator marks `REVIEW PENDING` immediately after a completed builder pass.
This makes an interrupted next invocation review the existing candidate instead
of rebuilding it. Only `tools/milestone_state.py` may perform either transition;
its chain verifier must always reach `COMPLETE` without skipping a dependency.

Historical milestone numbers in old ADR prose do not define current ordering.

## Current main sequence

| Milestone | State | Dependency | Contract |
|---|---|---|---|
| M01 WorldState + hierarchical Sidecar foundation | ACCEPTED | baseline | implemented baseline |
| M02 Communication foundation + block interaction | ACCEPTED | M01 | implemented baseline |
| M03 Router + gameplay Lua + scheduler + two-block proof | ACCEPTED | M02 | `docs/milestones/M03.md` |
| M04 enTT hot-state projection | OPEN | accepted M03 | `docs/milestones/M04.md` |
| M05 RocksDB persistence | OPEN | M04 | `docs/milestones/M05.md` |
| M06 Embedded client/server | OPEN | M05 | `docs/milestones/M06.md` |
| M07 Construction foundation | OPEN | M06 | `docs/milestones/M07.md` |
| M08 Construction blueprints/jobs | OPEN | M07 | `docs/milestones/M08.md` |
| M10 Hierarchical fast travel | OPEN | M08 (ordering only) | `docs/milestones/M10.md` |

`docs/ACTIVE_FINDINGS.md` contains only unresolved blockers for the currently
selected milestone, when any exist. This roadmap is the only status authority.
`tools/milestone_state.py` is the harness's sole status mutator; it can accept
only the currently eligible review-pending milestone and cannot skip
dependencies. Do not create another status ledger.

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
                        -> M10 hierarchical fast travel
```

The order is architectural: later milestones must consume the earlier public
contracts instead of replacing them.

## Preserved recovery reference

M10 ports useful hierarchy-aware navigation behavior onto the current
coordinate/DynamicSpace contracts. Its contract links the exact reference
code; earlier milestones do not need that material.

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

## Planning depth for remaining milestones

M04-M08 and M10 are no longer placeholders. Their contracts define state ownership,
threading, forbidden shortcuts, integration boundaries and mandatory proofs.
The planner may refine implementation ordering from current code, but may not
weaken those contracts or invent an alternate architecture.
