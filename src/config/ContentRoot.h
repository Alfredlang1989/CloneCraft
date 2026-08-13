#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace config
{
    /**
     * Resolved content root: the directory that owns all game content
     * (blocks, biomes, prototypes, textures, worldgen scripts, UI).
     *
     * Layout contract (see docs/ARCHITECTURE.md, "Content root"):
     *   <root>/MODS/<mod>/   one directory per installed mod
     *   <root>/MODS/Default/ always present; fallback when the configured
     *                        mod is missing or not installed
     *
     * The C++ core must be able to start with *no* content at all: when
     * neither the configured mod nor MODS/Default exists, path stays empty
     * and callers must skip all content-dependent initialization instead of
     * failing (the game may then show an emergency fallback screen).
     */
    struct ContentRoot
    {
        /** Active content directory (e.g. .../MODS/Default). Empty when no content was found. */
        std::filesystem::path path;

        /** Id of the resolved active mod. Empty when no content was found. */
        std::string mod;
    };

    /**
     * Resolves the active content root for a game installation directory.
     *
     * 1. configuredMod is a plain directory name (no separators or "." / "..")
     *    and <rootDir>/MODS/<configuredMod> exists  -> that directory
     * 2. otherwise <rootDir>/MODS/Default exists      -> Default (fallback)
     * 3. otherwise                                    -> empty path, empty mod
     *
     * Never throws; an invalid or missing configured mod silently falls back.
     */
    ContentRoot resolveContentRoot( const std::filesystem::path &rootDir,
                                    const std::string &configuredMod );

    /**
     * Content-root candidates for a running application, best first:
     * the executable's own directory (Linux /proc/self/exe; skipped when
     * unresolvable) and then the current working directory. The build
     * installs a MODS symlink next to the binary (see CMakeLists.txt), so
     * the game finds its content regardless of the launching directory.
     */
    std::vector<std::filesystem::path> contentRootCandidates();

    /**
     * Resolves the active content root by trying contentRootCandidates()
     * in order. Returns the first non-empty ContentRoot; when no candidate
     * has any content, an empty ContentRoot (core starts without content).
     */
    ContentRoot resolveContentRootFromCandidates( const std::string &configuredMod );
} // namespace config
