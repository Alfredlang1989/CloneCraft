#include "app/Application.h"

#include "core/Logging.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

#include <charconv>
#include <cstdlib>
#include <exception>
#include <string>
#include <system_error>

namespace
{
    /**
     * Pushes a synthetic SDL event through the real SDL event queue.
     * Used by the automated milestone-01 acceptance tests so that the
     * exact same code path as real input is exercised: SDL queue ->
     * InputManager -> Application::requestShutdown() / resize handler.
     */
    void pushEscapeKey( SDL_WindowID windowId )
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = windowId;
        event.key.scancode = SDL_SCANCODE_ESCAPE;
        event.key.key = SDL_GetKeyFromScancode( SDL_SCANCODE_ESCAPE, 0, true );
        if( !SDL_PushEvent( &event ) )
            core::logError( std::string( "SDL_PushEvent failed: " ) + SDL_GetError() );
    }

    void pushWindowCloseRequest( SDL_WindowID windowId )
    {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = windowId;
        if( !SDL_PushEvent( &event ) )
            core::logError( std::string( "SDL_PushEvent failed: " ) + SDL_GetError() );
    }
} // namespace

/**
 * Entry point: creates the Application, initializes subsystems and runs
 * the main loop until a clean shutdown is requested.
 *
 * Optional arguments (used by the automated smoke test):
 *   --exit-after-frames N   request shutdown after N rendered frames
 *   --help                  print usage
 *
 * Environment-driven smoke tests feed real SDL events into the queue
 * (identical pipeline to real keyboard/window-manager input):
 *   CLONECRAFT_SMOKE=esc     push ESC after 30 frames, expect clean exit
 *   CLONECRAFT_SMOKE=close   push window close request, expect clean exit
 *   CLONECRAFT_SMOKE=resize  push 900x600 resize, render a few more
 *                            frames, then clean exit (with -c N)
 */
int main( int argc, char *argv[] )
{
    int exitAfterFrames = 0;
    for( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if( arg == "--exit-after-frames" || arg == "-c" )
        {
            if( i + 1 >= argc )
            {
                core::logError( "Missing value for --exit-after-frames" );
                return 2;
            }
            const std::string value = argv[++i];
            const char *first = value.data();
            const char *last = first + value.size();
            const auto parsed = std::from_chars( first, last, exitAfterFrames );
            if( parsed.ec != std::errc{} || parsed.ptr != last || exitAfterFrames < 0 )
            {
                core::logError( "Invalid non-negative integer for --exit-after-frames: " + value );
                return 2;
            }
        }
        else if( arg == "--help" )
        {
            core::logInfo( "Usage: clonecraft [-c N] [--help]" );
            return 0;
        }
        else
        {
            core::logWarn( std::string( "Unknown argument: " ) + arg );
        }
    }

    const char *smokeEnv = std::getenv( "CLONECRAFT_SMOKE" );
    const std::string smoke = smokeEnv ? smokeEnv : "";

    app::Application application;
    if( !application.initialize() )
    {
        core::logError( "Application initialization failed - exiting" );
        return 1;
    }

    if( !smoke.empty() )
        core::logInfo( "Automated smoke test active: " + smoke );

    const int eventFrame = 30;
    const int resizeWidth = 900;
    const int resizeHeight = 600;

    int frameCount = 0;
    bool eventPushed = false;
    try
    {
        while( application.isRunning() )
        {
            if( !smoke.empty() && !eventPushed && frameCount >= eventFrame )
            {
                if( smoke == "esc" )
                    pushEscapeKey( application.getWindowId() );
                else if( smoke == "close" )
                    pushWindowCloseRequest( application.getWindowId() );
                else if( smoke == "resize" )
                {
                    // Real resize request; the resulting X-level resize event
                    // then flows through the normal SDL event pipeline.
                    if( !application.resizeWindow( resizeWidth, resizeHeight ) )
                        core::logError( "Smoke test: SDL resize request failed" );
                }
                else
                    core::logWarn( "CLONECRAFT_SMOKE mode ignored: " + smoke );
                eventPushed = true;
            }

            if( exitAfterFrames > 0 && frameCount >= exitAfterFrames )
            {
                core::logInfo( "Automated frame limit reached - requesting shutdown" );
                application.requestShutdown();
            }

            application.runFrameUpdate();
            if( application.isRunning() )
                application.renderFrame();
            ++frameCount;
        }
    }
    catch( const std::exception &e )
    {
        // Rendering failures (including GLSL compilation errors) must not end
        // in std::terminate. Keep the diagnostic and tear SDL/Ogre down in the
        // normal dependency order so the next run starts from a clean state.
        core::logError( std::string( "Fatal runtime exception: " ) + e.what() );
        application.shutdown();
        core::logInfo( "Clean shutdown after runtime error" );
        return 3;
    }

    application.shutdown();
    core::logInfo( "Clean shutdown complete" );
    return 0;
}