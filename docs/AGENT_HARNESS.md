# Agent harness and Graphify workflow

CloneCraft uses a small repository-local harness so coding agents can orient themselves in the real code graph before making broad changes.

## Why Graphify is here

The existing architecture gate answers questions such as "is this include direction forbidden?" and clang-tidy answers C++ semantic/static-analysis questions. Graphify adds a third view: a queryable structural graph of definitions and relationships extracted from the repository.

It is useful for questions such as:

- which modules actually depend on this type or subsystem;
- which call paths cross an intended ownership boundary;
- whether a supposedly data-driven feature has leaked concrete semantics into generic C++;
- which files participate in a worldgen/render/streaming flow;
- what may be affected before a cross-module refactor starts.

Graphify does not replace source review, tests, `architecture_check.py`, clang-tidy, or project documentation.

## Project-local installation

The wrapper is `tools/graphify_agent.py` and pins the Graphify package version used by the project. It never uses sudo and never modifies system packages.

Bootstrap once per checkout/tool environment:

    python3 tools/graphify_agent.py bootstrap

The virtual environment is created below `.tools/` and is ignored by git.

If Python cannot create a virtual environment on the current machine, the harness fails with a diagnostic. An autonomous agent must not respond by installing OS packages. Record the missing dependency instead.

## Deterministic structural graph

The mandatory baseline uses Graphify's code-only extraction path:

    python3 tools/graphify_agent.py refresh

This invokes the pinned Graphify executable with `extract . --code-only` and writes into `graphify-out/`.

The code-only baseline is intentional:

- structural code extraction is local and deterministic;
- it does not require an API key;
- it avoids silently sending repository material to an external model backend;
- architecture-sensitive claims can be grounded in source-derived edges first.

A richer semantic graph over documentation may be generated deliberately by a human or an explicitly configured agent environment, but it is not required by this harness and must not be silently enabled.

## Query-first agent protocol

After refreshing the graph, query the actual task before broad repository search:

    python3 tools/graphify_agent.py query "where does worldgen cross the registry boundary?"

Good first queries describe the intended change, not a guessed filename. Examples:

- "show the call and dependency flow from chunk streaming into world generation"
- "what connects Lua worldgen fields to block mutation passes?"
- "which modules depend on DynamicSpace and which cross the world/dynamic bridge?"
- "show dependencies around the renderer boundary and SDL ownership"

The returned graph narrows the source-reading set. Agents must still inspect the relevant implementation, configuration, and tests before editing.

## Evidence discipline

Graphify distinguishes source-extracted relationships from inferred ones. CloneCraft uses the following rule:

- `EXTRACTED`: valid structural evidence, still interpreted in source context;
- `INFERRED`: a lead, never sufficient alone for an architectural conclusion.

If a graph result appears to contradict source or project documentation, inspect the implementation and determine which artifact is stale. Do not rewrite an invariant merely to agree with a generated graph.

## Generated output

`graphify-out/` is generated. The useful shareable artifacts are the graph/report/visualization and manifest produced by Graphify. Local caches and cost files are ignored by git.

Do not hand-edit generated graph files. Refresh them through the harness.

`.graphifyignore` excludes build products, vendored third-party code, local tool environments, saves/world data, Graphify output itself, and generated Ogre shader dumps so the project graph represents CloneCraft rather than its luggage compartment.

## Start-of-run checklist

1. Read `AGENTS.md`.
2. Read `INDEX.plan` and its required documents.
3. Inspect git status/log.
4. Run Graphify status/bootstrap/refresh.
5. Query the task and use the result to choose source files.
6. Verify graph findings in source/config/tests.

## End-of-run checklist

For architecture-sensitive code changes:

1. Refresh Graphify after edits.
2. Query the affected boundary/flow again and look for unexpected new dependencies.
3. Run `python3 tools/architecture_check.py --root .`.
4. Run `./compile.sh` when target dependencies are present.
5. Update project documentation when ownership or architecture changed.

This deliberately creates overlapping evidence: the graph maps what exists, the architecture gate rejects forbidden shapes, clang-tidy checks C++ semantics, tests check behavior, and documentation states intended ownership.
