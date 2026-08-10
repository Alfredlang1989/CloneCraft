#pragma once

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstddef>
#include <functional>

namespace input
{
    /**
     * Polls SDL events each frame and maps raw SDL input to engine signals:
     * shutdown (ESC / window close / SDL quit), resize notifications,
     * per-key press/release (e.g. WASD state), and relative mouse motion.
     */
    class InputManager
    {
    public:
        using ShutdownCallback = std::function<void()>;
        using ResizeCallback = std::function<void(int, int)>;
        using KeyCallback = std::function<void( int scancode, bool pressed )>;
        using MouseMotionCallback = std::function<void( float dx, float dy )>;

        InputManager() = default;

        void setOnShutdown( ShutdownCallback callback );
        void setOnResize( ResizeCallback callback );
        void setOnKey( KeyCallback callback );
        void setOnMouseMotion( MouseMotionCallback callback );

        /** Raw pressed-state snapshot addressed by SDL scancode. */
        bool isKeyDown( int scancode ) const
        {
            return scancode >= 0 && scancode < SDL_SCANCODE_COUNT
                       ? mKeys[static_cast<std::size_t>( scancode )] != 0
                       : false;
        }
        void setKeyDown( int scancode, bool pressed );

        void pollEvents();

    private:
        ShutdownCallback mOnShutdown;
        ResizeCallback mOnResize;
        KeyCallback mOnKey;
        MouseMotionCallback mOnMouseMotion;
        std::array<unsigned char, SDL_SCANCODE_COUNT> mKeys{};
    };
} // namespace input
