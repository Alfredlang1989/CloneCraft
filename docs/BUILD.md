# Building Omnigrid

The preferred entry point is the repository-local build helper:

```bash
./compile.sh
```

It checks the existing toolchain, configures CMake, builds in parallel and runs
the renderer-independent test suite. It never installs or modifies OS packages.
If SDL3, OgreNext, OgreNext-HLMS, OgreNext-Overlay or another required system dependency is unavailable,
the configure/build stops with the dependency error. `nlohmann/json.hpp` is vendored under
`third_party/nlohmann/` and is not a host dependency.

## M04/M05 dependencies

EnTT v3.16.0 is pinned as its official modular header distribution under
`third_party/entt/`. Its upstream commit, MIT license and SHA-256 inventory live
in `third_party/entt/vendor.json`. CMake exposes it as `EnTT::EnTT`; no EnTT OS
package or network fetch is used.

RocksDB is deliberately host-provided. M05 and every later milestone require
RocksDB development files version 8.9.0 or newer. On Ubuntu 24.04 Noble,
`librocksdb-dev` supplies the headers, link library, CMake/pkg-config metadata
and the matching `librocksdb8.9` runtime dependency. OmniGrid never vendors,
downloads or installs RocksDB. CMake exposes a detected host installation as
`OmniGrid::RocksDB`; M05 must make the backend target require it.

`python3 tools/check_host_dependencies.py --current` is the milestone-aware
preflight. A missing RocksDB installation is informational during M04 and a
hard stop when M05 is selected.

Useful variants:

```bash
./compile.sh --release
./compile.sh --debug --run
./compile.sh --chunk 32 --group 16 --clean
./compile.sh --jobs 16 --verbose
./compile.sh --help
```

The default build directory includes build type and the experimental world
geometry parameters, for example `build/debug-c16-g16`, so 16³ and 32³ chunk
experiments do not accidentally share one CMake cache.

Environment variables such as `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH` are
honoured unchanged. This is useful when SDL3/OgreNext are installed in a
non-standard prefix.

## Agent policy

Autonomous/local coding agents must not install or modify operating-system
packages. Project-local third-party clones/builds are allowed. If a required
system dependency is missing, the agent stops and reports it.

OpenCode harness ownership: the Builder uses targeted builds/tests during
implementation; the Planner owns Graphify refresh; the independent Reviewer is
the only agent that runs the final `./compile.sh` acceptance gate.

External build/test harnesses may use broader permissions only when the user
has explicitly authorized that environment.


## Debug HUD dependency

The graphical application now explicitly requires the pkg-config module
`OGRE-Next-Overlay`. The F5 HUD uses Ogre Overlay text rendering. Ogre font
rendering in turn uses HLMS Unlit, which is supplied by the existing
`OGRE-Next-Hlms` dependency and registered at runtime next to PBS.

## Static and architecture analysis

Normal `./compile.sh` now runs two gates before compiling:

```text
architecture_check.py
CMake configure / compile_commands.json
clang-tidy
build
ctest
```

`clang-tidy` is required by the normal helper but is never installed by the
script. Set `CLANG_TIDY=/path/to/clang-tidy` when it has a versioned/custom
name. For analysis without a build use:

```bash
./compile.sh --analyze-only
```

`--no-static-analysis` is an explicit emergency escape hatch for clang-tidy;
the architecture dependency gate still runs. See `docs/STATIC_ANALYSIS.md`.

## Lua 5.4

The data-driven worldgen runtime requires the Lua 5.4 runtime library. Omnigrid
uses a small in-tree declaration of the Lua 5.4 C ABI (`LuaApi.h`), so normal
Linux builds need the runtime library but do not depend on a distro-specific Lua
header path. CMake searches for `lua5.4`, `lua54` or equivalent names and fails
configuration clearly if no Lua 5.4 runtime is available.
