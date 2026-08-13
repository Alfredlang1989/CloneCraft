#include "platform/PlatformWindowBridge.h"

#include "core/Logging.h"

#include <SDL3/SDL.h>

namespace platform
{
    namespace
    {
        constexpr int INITIAL_WIDTH = 1280;
        constexpr int INITIAL_HEIGHT = 720;

    } // namespace

    PlatformWindowBridge::~PlatformWindowBridge() { shutdown(); }

    bool PlatformWindowBridge::initialize( int width, int height, bool fullscreen, bool resizable )
    {
        if( !SDL_Init( SDL_INIT_VIDEO ) )
        {
            core::logError( std::string( "SDL_Init failed: " ) + SDL_GetError() );
            return false;
        }

        SDL_WindowFlags flags = 0;
        if( resizable ) flags |= SDL_WINDOW_RESIZABLE;
        if( fullscreen ) flags |= SDL_WINDOW_FULLSCREEN;

        mWindow = SDL_CreateWindow( "Omnigrid",
                                    width > 0 ? width : INITIAL_WIDTH,
                                    height > 0 ? height : INITIAL_HEIGHT,
                                    flags );
        if( !mWindow )
        {
            core::logError( std::string( "SDL_CreateWindow failed: " ) + SDL_GetError() );
            SDL_Quit();
            return false;
        }

        core::logInfo( "SDL window created" );
        return true;
    }

    void PlatformWindowBridge::shutdown()
    {
        // initialize() either owns a live window or has already undone SDL
        // initialization on failure. This guard makes explicit shutdown +
        // destructor shutdown idempotent (no double SDL_Quit/log spam).
        if( !mWindow )
            return;

        SDL_DestroyWindow( mWindow );
        mWindow = nullptr;
        SDL_Quit();
        core::logInfo( "SDL shut down" );
    }

    bool PlatformWindowBridge::getNativeWindowInfo( NativeWindowInfo &out ) const
    {
        if( !mWindow )
            return false;

        SDL_PropertiesID props = SDL_GetWindowProperties( mWindow );

        void *display = SDL_GetPointerProperty( props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr );
        std::uint64_t x11Window =
            static_cast<std::uint64_t>( SDL_GetNumberProperty( props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0 ) );

        if( !display || x11Window == 0 )
        {
            // A Wayland path would land here in the future; for milestone 01
            // only the X11 backend is supported (see docs/DECISIONS.md).
            core::logError( "SDL window has no X11 native handle (Wayland is not supported in milestone 01)" );
            return false;
        }

        out.x11Display = display;
        out.x11Window = x11Window;
        out.x11Screen = static_cast<int>(
            SDL_GetNumberProperty( props, SDL_PROP_WINDOW_X11_SCREEN_NUMBER, 0 ) );

        if( !getWindowSize( out.widthPx, out.heightPx ) )
            return false;

        return true;
    }

    bool PlatformWindowBridge::getWindowSize( int &width, int &height ) const
    {
        if( !mWindow )
            return false;
        return SDL_GetWindowSize( mWindow, &width, &height );
    }

    std::uint32_t PlatformWindowBridge::getWindowId() const
    {
        return mWindow ? SDL_GetWindowID( mWindow ) : 0;
    }

    bool PlatformWindowBridge::resizeTo( int width, int height )
    {
        if( !mWindow )
            return false;
        return SDL_SetWindowSize( mWindow, width, height );
    }

    bool PlatformWindowBridge::setRelativeMouseMode( bool enabled )
    {
        if( !mWindow )
            return false;
        return SDL_SetWindowRelativeMouseMode( mWindow, enabled );
    }

} // namespace platform