#pragma once

#include <cstdint>
#include <string>

namespace core
{
    enum class LogLevel : std::uint8_t
    {
        Info,
        Warn,
        Error,
    };

    void log( LogLevel level, const std::string &message );

    inline void logInfo( const std::string &message ) { log( LogLevel::Info, message ); }
    inline void logWarn( const std::string &message ) { log( LogLevel::Warn, message ); }
    inline void logError( const std::string &message ) { log( LogLevel::Error, message ); }
} // namespace core