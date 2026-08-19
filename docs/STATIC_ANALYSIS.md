# Static analysis and architecture gates

The repository has complementary gates. They answer different questions and no
green gate substitutes for another.

## Deterministic architecture checker

`tools/architecture_check.py` enforces the module/source dependency firewall
from `tools/architecture_rules.json`.

It checks module classification, allowed include direction, forbidden framework
leaks, module/include cycles, duplicate header basenames, source-pattern
invariants and large-translation-unit warnings.

The checker supports parallel file inspection through `--jobs` (default bounded
by available CPUs) while merging results deterministically.

Fast standalone use:

```bash
python3 tools/architecture_check.py --root .
python3 tools/architecture_check.py --root . --jobs 8
```

## Graphify

Graphify is architecture analysis, not a Builder completion tool.

In the OpenCode harness the Planner owns the refresh. After every Builder pass
and before its Reviewer pass the Planner runs exactly one:

```bash
graphify update .
```

and then targeted `graphify query/path/explain` checks. Builder and Reviewer do
not refresh the graph. Historical Graphify snapshots are not mandatory reading.

## clang-tidy AST/static analysis

`tools/run_static_analysis.py` runs clang-tidy against project translation units
from `compile_commands.json`. It is parallel by default with a bounded
`ThreadPoolExecutor`; `--jobs N` overrides concurrency.

The rule set lives in `.clang-tidy`. The tool never installs clang-tidy or OS
packages.

## Full acceptance helper

`./compile.sh` performs the full ordered gate:

```text
architecture_check.py
CMake configure / compile_commands.json
clang-tidy static analysis
build
ctest
```

Harness ownership is strict:

- Planner: architecture planning + Graphify refresh; never `compile.sh`.
- Builder: targeted builds/tests; never `compile.sh`, never Graphify refresh.
- Reviewer: only agent that runs final `./compile.sh` after cheap review stages
  find no blocker.

Do not change a checker/rule merely to silence a valid patch failure. If an
architecture rule itself must change, that requires an explicit architecture
contract change rather than a local workaround.
