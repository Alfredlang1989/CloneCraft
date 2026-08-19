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
        /** M02-D: mouse button edges (button 1 = primary, 3 = secondary). */
        using MouseButtonCallback = std::function<void( int button, bool pressed )>;

        InputManager() = default;

        void setOnShutdown( ShutdownCallback callback );
        void setOnResize( ResizeCallback callback );
        void setOnKey( KeyCallback callback );
        void setOnMouseMotion( MouseMotionCallback callback );
        void setOnMouseButton( MouseButtonCallback callback );

        /** Raw pressed-state snapshot addressed by SDL scancode. */
        bool isKeyDown( int scancode ) const
        {
            return scancode >= 0 && scancode < SDL_SCANCODE_COUNT
                       ? mKeys[static_cast<std::size_t>( scancode )] != 0
                       : false;
        }
        void setKeyDown( int scancode, bool pressed );

        /** Pressed-state snapshot addressed by SDL mouse button (1=left,
         *  2=middle, 3=right). */
        bool isMouseButtonDown( int button ) const
        {
            return button >= 0 && button < static_cast<int>( mButtons.size() )
                       ? mButtons[static_cast<std::size_t>( button )] != 0
                       : false;
        }
        void setMouseButton( int button, bool pressed );

        void pollEvents();

    private:
        ShutdownCallback mOnShutdown;
        ResizeCallback mOnResize;
        KeyCallback mOnKey;
        MouseMotionCallback mOnMouseMotion;
        MouseButtonCallback mOnMouseButton;
        std::array<unsigned char, SDL_SCANCODE_COUNT> mKeys{};
        std::array<unsigned char, 8> mButtons{}; // SDL button 1..7
    };
} // namespace input
