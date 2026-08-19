#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace world::communication
{
    /**
     * Error reported by envelope validation/routing. Messages mention the
     * offending field and the reason, mirroring RegistryError's style.
     */
    class CommunicationError : public std::runtime_error
    {
    public:
        explicit CommunicationError( const std::string &message ) :
            std::runtime_error( message )
        {
        }
    };
} // namespace world::communication