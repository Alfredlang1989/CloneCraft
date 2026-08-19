#pragma once

#include "world/communication/CommunicationRuntime.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/coordinates/Coords.h"
#include "world/registry/BlockIdTable.h"
#include "world/scripting/GameplayLuaRuntime.h"
#include "world/state/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace world::scripting
{
    class GameplayContentError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /**
     * Generic content bootstrap for owner-thread gameplay Lua.
     *
     * `gameplay.json` owns script files, router bindings and optional
     * one-shot placements/invocations. The engine sees only mechanisms and
     * opaque namespaced ids. Bootstrap placement waits until every target
     * chunk is already materialized, then enters the ordinary block-command
     * -> CommunicationRuntime -> WorldState path. Declared replacement is an
     * explicit remove followed by place.
     */
    class GameplayContentRuntime
    {
    public:
        using SurfaceHeightResolver =
            std::function<std::int64_t( const BlockAddress &column )>;

        /** Returns null when `<contentRoot>/gameplay.json` is absent. */
        static std::shared_ptr<GameplayContentRuntime> loadIfPresent(
            const std::filesystem::path &contentRoot,
            communication::CommunicationRuntime &bus,
            communication::DelayedMessageScheduler &scheduler,
            WorldState &world,
            const BlockIdTable &idTable,
            SurfaceHeightResolver surfaceHeight );

        ~GameplayContentRuntime();
        GameplayContentRuntime( const GameplayContentRuntime & ) = delete;
        GameplayContentRuntime &operator=( const GameplayContentRuntime & ) = delete;

        /** Runs every bootstrap whose target chunks are now materialized.
         *  Returns the number completed by this call. */
        std::size_t updateBootstraps();
        std::size_t pendingBootstrapCount() const;

        /** Resolved canonical address of a named manifest placement. */
        std::optional<BlockAddress> placementAddress( const std::string &id ) const;

        /** Owner-thread Lua handle used by focused tests/tools and lifecycle
         *  integrations. Production router bindings retain only weak refs. */
        std::shared_ptr<GameplayLuaRuntime> luaRuntime() const;

        // Public name only so the implementation's parsing helpers can refer
        // to it; the definition and all state remain private to the .cpp.
        struct State;

    private:
        explicit GameplayContentRuntime( std::unique_ptr<State> state );
        std::unique_ptr<State> mState;
    };
} // namespace world::scripting
