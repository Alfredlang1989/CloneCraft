#pragma once

#include "world/registry/Registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace world
{
    /**
     * Loads and validates registry JSON files. Every field is checked with
     * a precise error message (source, entry number, offending field),
     * duplicated ids are rejected, cross-references (biome -> block,
     * resource -> block) are validated *after* all files are parsed.
     *
     * File layout (expected names):
     *   <dir>/blocks.json      -> array "blocks"
     *   <dir>/biomes.json      -> array "biomes"
     *   <dir>/resources.json   -> array "resources"
     */
    class RegistryLoader
    {
    public:
        /**
         * Loads blocks.json, biomes.json and resources.json from a
         * directory and performs all cross-reference checks.
         * Throws RegistryError on any problem.
         */
        static void loadFromDirectory( const std::filesystem::path &dir,
                                       BlockRegistry &blocks,
                                       BiomeRegistry &biomes,
                                       ResourceRegistry &resources );

        /** Parses one already-read JSON document (used by tests). */
        static void parseBlocks( const nlohmann::json &root, const std::string &source,
                                 BlockRegistry &out );
        static void parseBiomes( const nlohmann::json &root, const std::string &source,
                                 BiomeRegistry &out, const BlockRegistry &blocks );
        static void parseResources( const nlohmann::json &root, const std::string &source,
                                    ResourceRegistry &out, const BlockRegistry &blocks );

        /** Parses a JSON file that has already been loaded as text. */
        static void parseBlockFile( const std::string &text, const std::string &source,
                                    BlockRegistry &out );

        /**
         * Loads <dir>/prototypes.json into `out` when the file exists.
         * Returns false when the file is absent (a content root without
         * prototypes is legal); throws RegistryError on any parse or
         * validation problem.
         *
         * When `sidecars` is non-null, each prototype property is validated
         * against sidecars.json at load time (ADR-027): the property id must
         * resolve to a registered sidecar type and the prototype default must
         * fit that type's valueType/bitWidth. A mod that declares a property
         * without a backing sidecar type, or a default that cannot be stored,
         * is rejected instead of silently degrading later.
         */
        static bool loadPrototypes( const std::filesystem::path &dir,
                                    const BlockRegistry &blocks,
                                    PrototypeRegistry &out,
                                    const SidecarRegistry *sidecars = nullptr );

        /** Parses one already-read JSON document (used by tests). */
        static void parsePrototypes( const nlohmann::json &root, const std::string &source,
                                     const BlockRegistry &blocks, PrototypeRegistry &out,
                                     const SidecarRegistry *sidecars = nullptr );

        /**
         * Loads <dir>/sidecars.json into `out` when the file exists.
         * Returns false when the file is absent (a content root without
         * sidecar types is legal); throws RegistryError on any problem.
         */
        static bool loadSidecars( const std::filesystem::path &dir, SidecarRegistry &out );

        /** Parses one already-read JSON document (used by tests). */
        static void parseSidecars( const nlohmann::json &root, const std::string &source,
                                   SidecarRegistry &out );

    private:
        static std::string requireString( const nlohmann::json &object,
                                          const std::string &source, int index,
                                          const char *field );
        static void checkUnknownFields( const nlohmann::json &object,
                                        const std::string &source, int index,
                                        const char *const allowed[], size_t count );
    };
} // namespace world