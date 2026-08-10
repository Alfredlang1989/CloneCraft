# MISSING DEPENDENCIES / ENVIRONMENT NOTES

This file is **machine-specific**. Future agents must re-run the environment
audit instead of assuming the packages listed by an older run are still
present.

## Repair execution environment (2026-08-08)

A full CMake configure was attempted with:

```text
cmake -S . -B /mnt/data/clonecraft_cmake_check -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

It stopped at `find_package(SDL3 3.0 REQUIRED)` because this execution
container does not provide `SDL3Config.cmake` / `sdl3-config.cmake`. The same
container also does not expose the uploaded developer machine's OgreNext setup.
`nlohmann/json.hpp` is now bundled in-tree and no longer depends on the host.

**No operating-system package was installed or modified.** Renderer-independent
sources/tests were checked separately; see `docs/STATUS.md` and
`docs/CODE_AUDIT.md`.

The uploaded prototype/build metadata indicates that the actual development
machine previously had SDL3/OgreNext available. Re-check that machine before
claiming a dependency is missing there.

## Future milestone dependencies

- Jolt Physics: required later for the physics milestone. Prefer a project-local
  dependency under `third_party/`/FetchContent if it is not already available.
- Lua 5.4: required later for scripting/mod behaviour. Prefer a project-local
  source build if suitable development headers are not already available.

## Agent policy

Never use `apt`, `dnf`, `yum`, `pacman`, `zypper`, `brew`, `sudo make install`,
or otherwise modify the OS to satisfy this project. Project-local clones/builds
are allowed. If a required system dependency cannot be satisfied locally, stop
and report exactly what is missing.
