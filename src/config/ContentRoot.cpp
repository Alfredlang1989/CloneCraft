#include "config/ContentRoot.h"

#include <filesystem>

namespace config
{
    namespace
    {
        bool isSafeModName( const std::string &name )
        {
            if( name.empty() || name == "." || name == ".." )
                return false;
            return name.find( '/' ) == std::string::npos &&
                   name.find( '\\' ) == std::string::npos;
        }

        bool directoryExists( const std::filesystem::path &path )
        {
            std::error_code error;
            const bool exists = std::filesystem::is_directory( path, error );
            return exists && !error;
        }
    } // namespace

    ContentRoot resolveContentRoot( const std::filesystem::path &rootDir,
                                    const std::string &configuredMod )
    {
        ContentRoot result;

        const std::filesystem::path modsDir = rootDir / "MODS";

        if( isSafeModName( configuredMod ) )
        {
            const std::filesystem::path candidate = modsDir / configuredMod;
            if( directoryExists( candidate ) )
            {
                result.path = candidate;
                result.mod = configuredMod;
                return result;
            }
        }

        const std::filesystem::path fallback = modsDir / "Default";
        if( directoryExists( fallback ) )
        {
            result.path = fallback;
            result.mod = "Default";
            return result;
        }

        result.path.clear();
        result.mod.clear();
        return result;
    }
} // namespace config
