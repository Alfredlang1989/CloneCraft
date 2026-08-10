# TARGET BUILD ENVIRONMENT

This is the environment fingerprint observed from Alfred's real Clonecraft
build on 2026-08-08. It is the compatibility target for renderer integration.

```text
OS family      : Ubuntu 24.04 (Noble)
Compiler       : c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
CMake          : 3.28.3
Generator      : Ninja
pkg-config     : available (1.8.1 was reported by CMake)
OgreNext       : 4.0.0unstable
OgreNext-Hlms  : 4.0.0unstable
nlohmann_json  : 3.11.3 (/usr/share/cmake/nlohmann_json/...) [host package observed; Clonecraft now uses bundled 3.12.0]
SDL3           : exact version not present in the first build log
Window backend : X11 integration used by Clonecraft
```

`./compile.sh --fingerprint` prints the SDL3 version too when the local
`pkg-config sdl3` module is available. Keep this file updated from the actual
development machine rather than guessing versions.

## Harness note

An external validation harness may install packages when explicitly authorised
by the user. The repository's no-system-install rule applies to autonomous
agents running on the user's workstation. It does not forbid an isolated test
harness from being modified.

The current OpenAI execution container itself is Debian-based and does not
expose the target machine's SDL3/OgreNext installation. Its outbound system
package/source downloads are restricted by the runtime, so it cannot be
truthfully described as an exact Ubuntu 24.04/OgreNext clone. Renderer-independent
code is therefore additionally compiled there with a strict newer compiler,
while the real target build remains authoritative for SDL/Ogre ABI integration.
