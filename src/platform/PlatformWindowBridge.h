#pragma once

#include <cstdint>

struct SDL_Window;

namespace platform
{
    /**
     * Native window handles extracted from the SDL window.
     * Currently only the X11 path is supported (this machine runs X11).
     * Gameplay code must never see these values; only the renderer uses
     * them to attach OgreNext to the SDL-owned window.
     */
    struct NativeWindowInfo
    {
        void *x11Display = nullptr; // X11 Display*
        std::uint64_t x11Window = 0; // X11 ::Window (XID)
        int x11Screen = 0;
        int widthPx = 0;
        int heightPx = 0;
    };

    /**
     * SDL owns the native application window (SDL_Init, SDL_CreateWindow,
     * SDL_PollEvent, window state).
     *
     * PlatformWindowBridge exposes the native representation of the SDL
     * window to the renderer (which binds OgreNext to it). No Ogre/SDL
     * internals leak into gameplay code.
     */
    class PlatformWindowBridge
    {
    public:
        ~PlatformWindowBridge();

        bool initialize( int width, int height, bool fullscreen = false, bool resizable = true );
        void shutdown();

        bool getNativeWindowInfo( NativeWindowInfo &out ) const;
        bool getWindowSize( int &width, int &height ) const;
        std::uint32_t getWindowId() const;

        /** Requests a window resize through SDL (real X request). */
        bool resizeTo( int width, int height );

        /** Grabs/releases the cursor (relative mouse motion for FPS look). */
        bool setRelativeMouseMode( bool enabled );


    private:
        SDL_Window *mWindow = nullptr;
    };
} // namespace platform