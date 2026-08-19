#include "input/InputManager.h"

#include "core/Logging.h"

#include <SDL3/SDL.h>

#include <utility>

namespace input
{
    void InputManager::setOnShutdown( ShutdownCallback callback ) { mOnShutdown = std::move( callback ); }
    void InputManager::setOnResize( ResizeCallback callback ) { mOnResize = std::move( callback ); }
    void InputManager::setOnKey( KeyCallback callback ) { mOnKey = std::move( callback ); }
    void InputManager::setOnMouseMotion( MouseMotionCallback callback )
    {
        mOnMouseMotion = std::move( callback );
    }

    void InputManager::setOnMouseButton( MouseButtonCallback callback )
    {
        mOnMouseButton = std::move( callback );
    }

    void InputManager::setKeyDown( int scancode, bool pressed )
    {
        if( scancode >= 0 && scancode < SDL_SCANCODE_COUNT )
            mKeys[static_cast<std::size_t>( scancode )] = pressed ? 1u : 0u;
    }

    void InputManager::setMouseButton( int button, bool pressed )
    {
        if( button >= 0 && button < static_cast<int>( mButtons.size() ) )
            mButtons[static_cast<std::size_t>( button )] = pressed ? 1u : 0u;
    }

    void InputManager::pollEvents()
    {
        SDL_Event event;
        while( SDL_PollEvent( &event ) )
        {
            switch( event.type )
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if( mOnShutdown )
                    mOnShutdown();
                break;

            case SDL_EVENT_KEY_DOWN:
                setKeyDown( event.key.scancode, true );

                // Key-state remains pressed during OS repeat, but callbacks
                // represent edges and must not fire dozens of fake presses.
                if( !event.key.repeat )
                {
                    if( event.key.scancode == SDL_SCANCODE_ESCAPE )
                    {
                        core::logInfo( "ESC pressed - shutdown requested" );
                        if( mOnShutdown )
                            mOnShutdown();
                    }
                    if( mOnKey )
                        mOnKey( event.key.scancode, true );
                }
                break;

            case SDL_EVENT_KEY_UP:
                setKeyDown( event.key.scancode, false );
                if( mOnKey )
                    mOnKey( event.key.scancode, false );
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if( mOnMouseMotion )
                    mOnMouseMotion( event.motion.xrel, event.motion.yrel );
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                setMouseButton( static_cast<int>( event.button.button ), true );
                if( mOnMouseButton )
                    mOnMouseButton( static_cast<int>( event.button.button ), true );
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                setMouseButton( static_cast<int>( event.button.button ), false );
                if( mOnMouseButton )
                    mOnMouseButton( static_cast<int>( event.button.button ), false );
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                if( mOnResize )
                    mOnResize( event.window.data1, event.window.data2 );
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Do not leave WASD/Shift logically held if the window loses
                // focus before SDL delivers matching KEY_UP events; the same
                // applies to mouse buttons (M02 review round 3).
                mKeys.fill( 0u );
                mButtons.fill( 0u );
                break;

            default:
                break;
            }
        }
    }
} // namespace input