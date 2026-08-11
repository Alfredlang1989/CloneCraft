# CloneCraft coding-agent contract

This file is the first entry point for every coding agent working in this repository.
It applies to all agent frameworks and model vendors.

## Mandatory orientation

Before changing code or data:

1. Read `INDEX.plan` and every document listed in its `READ FIRST ON EVERY AGENT RUN` section.
2. Inspect `git status` and the recent git log. Preserve unrelated local work.
3. Use the repository Graphify harness before broad code exploration:
   - `python3 tools/graphify_agent.py status`
   - if the local project tool is missing: `python3 tools/graphify_agent.py bootstrap`
   - refresh the structural graph: `python3 tools/graphify_agent.py refresh`
   - query the graph with the actual task or architectural question: `python3 tools/graphify_agent.py query "<question>"`
4. Follow graph results to the relevant source files, then verify behavior in source/config/tests. Graphify is an orientation and dependency tool, not a substitute for reading the implementation.

For architecture, ownership, call-flow, data-flow, cross-module, worldgen, coordinate, renderer, simulation, ECS/sidecar, scripting, registry, or dispatcher work, Graphify use is mandatory. Do not start with repository-wide grep when a graph query can narrow the search first.

## Graph evidence rules

- Treat `EXTRACTED` edges as source-derived structural evidence.
- Treat `INFERRED` edges as useful leads that must be verified before making an architectural claim.
- Never hand-edit `graphify-out/graph.json`, `graphify-out/graph.html`, `graphify-out/GRAPH_REPORT.md`, or Graphify manifests/caches.
- Generated graph output may be stale after edits. Refresh it before final architectural conclusions and before proposing a completed architecture-sensitive change.
- The code graph does not override explicit project invariants in `INDEX.plan`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, or the architecture gate. If graph and source/docs disagree, investigate and update the stale side deliberately.

## Repository safety

- Do not install or modify operating-system packages.
- Graphify is a development tool, not a CloneCraft runtime dependency. The repository wrapper installs the pinned tool only under `.tools/`.
- Do not weaken `tools/architecture_rules.json`, `.clang-tidy`, tests, or validation scripts merely to make a change pass.
- Generic engine modules must not absorb content-specific semantics just because doing so is convenient.
- Preserve deterministic worldgen and the hierarchical-coordinate invariants.

## Mandatory completion path

For code changes:

1. Refresh Graphify after the edit and query the affected flow/boundary again.
2. Run `python3 tools/architecture_check.py --root .`.
3. Run the normal `./compile.sh` gate when target dependencies are available. Never claim the graphical target built when SDL3/OgreNext were unavailable.
4. Update architecture/status/decision documentation when the implemented architecture changed.

The goal is not to make agents obey a diagram. The goal is to make them inspect the actual dependency graph before touching the gearbox, then prove they did not leave a handful of loose cogs behind.
