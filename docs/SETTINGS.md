# Clonecraft settings

Clonecraft keeps user/runtime settings separate from shipped game data.

On Linux the settings file is stored exactly at `$HOME/.config/Clonecraft/settings.json` (normally `~/.config/Clonecraft/settings.json`).

If the file does not exist, Clonecraft creates the directory and writes a complete default settings file atomically before continuing startup.

## Schema version 1

The initial file contains four groups:

- `window`: startup resolution, fullscreen and resizable-window state.
- `world`: 3D chunk streaming/render radius and the main-thread commit budget per update.
- `camera`: movement speed and mouse-look sensitivity.
- `ogre`: render system/plugin, Ogre log filename, free-form RenderSystem `config_options`, shadow distance, camera clip distances and Forward3D light-grid settings.

`ogre.config_options` is deliberately a string-to-string object. Values are passed to Ogre's selected RenderSystem before window creation. This makes backend-specific options extensible without growing a new C++ field for every Ogre option. Unsupported options are logged and ignored so a driver/backend difference does not make the game unbootable.

The current `chunk_render_distance` is the Chebyshev radius in chunks used by the existing 3D streamer. A value of 3 therefore covers a 7 x 7 x 7 chunk cube around the player's current chunk, subject to asynchronous generation and eviction.

A future in-game settings menu can use the same `config::saveSettings()` path; the persistence layer is not tied to a UI implementation.

## Remaining renderer-side tuning

The settings layer is intentionally extensible but does not yet own every visual
constant. Sky/background colour, ambient/sun parameters, flashlight colour/power/cone
and detailed PSSM/shadow-map tuning still live in `OgreRenderer.cpp`. Those are the
main remaining render-settings candidates; block materials and UI selection styling
are already data-owned elsewhere.
