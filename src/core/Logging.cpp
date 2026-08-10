#include "core/Logging.h"

#include <iostream>

namespace core
{
    void log( LogLevel level, const std::string &message )
    {
        switch( level )
        {
        case LogLevel::Info: std::cout << "[INFO]  "<< message << '\n'; break;
        case LogLevel::Warn: std::cout << "[WARN]  "<< message << '\n'; break;
        case LogLevel::Error: std::cerr << "[ERROR] "<< message << '\n'; break;
        }
    }
} // namespace core