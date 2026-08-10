# Static analysis and architecture gate

Clonecraft now has two complementary repository-local analysis gates.

## 1. clang-tidy: established C++ AST / semantic analysis

`tools/run_static_analysis.py` runs `clang-tidy` against CMake's
`compile_commands.json` for every project translation unit under `src/` and
`tests/`. `third_party/` is deliberately excluded. The runner passes
`--quiet`, so clang-tidy does not print tens of thousands of suppressed
third-party/system-header diagnostic counters; real project diagnostics remain
visible.

The rule set lives in `.clang-tidy` and currently enables:

- `clang-analyzer-*`
- `bugprone-*` (except `bugprone-forward-declaration-namespace`, disabled
  because OgreNext intentionally exposes v1/v2 forward declarations with the
  same class names and triggers that check spuriously)
- `performance-*`
- `portability-*`
- selected low-noise modernization/readability checks

`clang-analyzer-*` diagnostics are warnings-as-errors. Other enabled checks are
reported and can be tightened after the initial warning baseline is known on the
target toolchain.

`.clang-tidy` uses clang-tidy's native YAML configuration format. This is build/tooling
metadata only; Clonecraft runtime/content configuration remains JSON and no YAML parser
is added to the game.

`clang-tidy` is not vendored and `compile.sh` never installs it. The local
coding-agent policy forbids modifying the OS. If the executable is missing,
analysis stops and reports the missing tool. A custom/versioned executable can
be selected with:

```bash
CLANG_TIDY=/path/to/clang-tidy ./compile.sh
```

Normal `./compile.sh` runs the AST pass by default. `--analyze-only` performs
configuration + architecture + static analysis without compiling. The
`--no-static-analysis` switch exists only as an explicit emergency escape hatch;
the architecture gate still runs.

## 2. Clonecraft architecture checker

`tools/architecture_check.py` is a small deterministic include/dependency gate.
Its rules are JSON in `tools/architecture_rules.json`.

It checks:

- every source file belongs to a declared module;
- project-local include dependencies follow the allowed direction;
- forbidden external framework leaks are blocked (for example Ogre/SDL headers
  entering world/core modules, or SDL entering the renderer);
- the module dependency graph contains no cycles;
- the project-local include graph contains no include cycles;
- no two project headers have the same basename;
- source-pattern invariants from JSON, including the huge-world precision guard (no global camera XYZ fields/accessors, no absolute camera-pose API, no direct chunk/group-to-float cast);
- very large C++ translation units are reported as refactor warnings.

This intentionally catches architecture mistakes that clang-tidy does not know
about, for example world code starting to include Ogre headers or two unrelated
modules both defining `Chunk.h`.

Current dependency direction is approximately:

```text
core / coordinates / registry
          |
          +--> worldgen
          |       |
          +--> chunk
          |       |
          +--> mesh
                  |
platform --------> render
camera/input ----> app <---- debug formatter
                    |
                   main
```

The exact authoritative rule graph is `tools/architecture_rules.json`.

## CMake helpers

When Python is available CMake exposes:

```bash
cmake --build <build-dir> --target architecture_check
```

When clang-tidy is also available:

```bash
cmake --build <build-dir> --target static_analysis
```

`compile.sh` remains the preferred entry point because it runs the gates in the
required order before the normal build and tests.

## Agent rule

Every future coding-agent run must read `INDEX.plan`, inspect `git status`, and
use the normal analysis/build path before claiming a milestone complete.
Do not silence an architecture/static-analysis failure by changing the checker
unless the architectural rule itself is being deliberately changed and the
reason is documented.
