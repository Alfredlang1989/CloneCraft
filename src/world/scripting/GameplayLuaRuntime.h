#pragma once

#include "world/communication/CommunicationRegistries.h"
#include "world/communication/CommunicationRuntime.h"
#include "world/communication/DelayedMessageScheduler.h"
#include "world/state/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace world::scripting
{
    using world::WorldState;
    using world::communication::CommunicationEnvelope;
    using world::communication::CommunicationRuntime;
    using world::communication::DelayedMessageScheduler;
    using world::communication::Handler;

    /** Defined, recoverable gameplay-scripting error (Lua compile/runtime/
     *  budget/API misuse). Crosses the C++ <-> Lua boundary only through a
     *  protected call - C++ exceptions never unwind through the Lua C ABI. */
    class GameplayLuaError : public std::runtime_error
    {
    public:
        explicit GameplayLuaError( const std::string &message ) : std::runtime_error( message ) {}
    };

    /** Budget abort (M03 Round 3 hardening): a script exceeded its
     *  instruction budget. Deliberately a DISTINCT type so the bridge
     *  trampolines can re-raise it as the private host sentinel instead of a
     *  swallowable string error - neither pcall nor xpcall may ever return a
     *  budget abort into script control. The host invoke() surface still
     *  reports it as a GameplayLuaError ("instruction budget exceeded"). */
    class GameplayLuaBudgetError : public GameplayLuaError
    {
    public:
        using GameplayLuaError::GameplayLuaError;
    };

    /** Host-defined binding record for a Lua bus handler.
     *
     * `principal` is the security/authorization identity of this handler.
     * Lua can NEVER choose its own sender: every outbound message gets
     * `sender = binding.principal` assigned by C++, and message ids always
     * come from the CommunicationRuntime sequence. */
    struct ScriptBinding
    {
        std::string scriptId;
        std::string functionName;
        std::string principal;
    };

    /**
     * Gameplay Lua runtime (M03 Round 3).
     *
     * Owner/Game-thread only: Lua is deliberately NOT thread-safe. One shared
     * Lua state with one isolated _ENV per script - scripts never see each
     * other's globals and never see the un-filtered real _G. The standard
     * library is a controlled sandbox (no os/io/debug/package/require/
     * loadfile/dofile/load/coroutine, no math.random/randomseed). A
     * configurable instruction budget (Lua debug hook) prevents scripts from
     * freezing the game thread; no wall-time timeouts, no watchdog threads.
     *
     * Lua communicates EXCLUSIVELY through the one CommunicationRuntime
     * (bus.send/bus.query/bus.reply) and the DelayedMessageScheduler
     * (bus.schedule_after_ms). There is no second event bus, no Lua callback
     * timer and no Lua reference stored in the scheduler - the scheduler
     * keeps transporting plain CommunicationEnvelopes (Round-2 contract).
     * WorldState is exposed READ ONLY (world.get_block).
     *
     * Ownership: this runtime references the CommunicationRuntime, the
     * DelayedMessageScheduler and the WorldState and MUST be destroyed before
     * them. Construction is deliberately restricted to create(), whose
     * shared_ptr deleter enforces final release on the owner thread (a foreign
     * final release terminates before any Lua teardown can run). Host wiring
     * keeps a std::shared_ptr<GameplayLuaRuntime>;
     * ActionRegistry bridge handlers (bridgeHandler) capture a std::weak_ptr
     * and become a safe no-op once the runtime is gone - no dangling raw
     * pointer, provable destruction order.
     */
    class GameplayLuaRuntime
    {
    public:
        /** Default instruction budget per script invocation. */
        static constexpr std::uint64_t kDefaultInstructionBudget = 1'000'000;

        /** Creates the only supported owning handle. The final shared_ptr
         *  release MUST occur on the creating/owner thread; violating that
         *  contract terminates before luaL_unref/lua_close can run. */
        static std::shared_ptr<GameplayLuaRuntime> create(
            CommunicationRuntime &bus, DelayedMessageScheduler &scheduler,
            const WorldState &world,
            std::uint64_t instructionBudget = kDefaultInstructionBudget );

        GameplayLuaRuntime( const GameplayLuaRuntime & ) = delete;
        GameplayLuaRuntime &operator=( const GameplayLuaRuntime & ) = delete;

        /** Loads `source` as script `scriptId`, giving it a fresh isolated _ENV
         *  with its own copies of the sandbox namespace tables
         *  (bus/world/math/string/table/utf8 - the table OBJECTS are never
         *  shared between scripts). A duplicate `scriptId` is rejected loudly
         *  (hot reload is NOT part of Round 3). The chunk's top-level code
         *  runs budgeted on the owner thread. Throws GameplayLuaError on
         *  load/compile/runtime/budget errors. */
        void loadScript( const std::string &scriptId, const std::string &source );
        bool hasScript( const std::string &scriptId ) const;
        bool hasFunction( const std::string &scriptId, const std::string &functionName ) const;

        /** Maximum nested synchronous invocations (query -> query -> ...)
         *  before a defined error (default 32). Owner-thread enforced. */
        void setMaxInvocationDepth( std::size_t depth );
        std::size_t maxInvocationDepth() const;

        /** Per-invocation Lua instruction budget (debug-count hook,
         *  installed ONCE per state - nested invocations can never re-arm
         *  it). Owner-thread enforced. */
        void setInstructionBudget( std::uint64_t budget );
        std::uint64_t instructionBudget() const;

        /** Invokes a script handler on the owner thread. The handler receives
         *  the envelope as a read-only snapshot and may use the bus/world
         *  bindings; its bus.reply() outputs are collected into `replies`
         *  (validated later by the existing OutputContract). Throws
         *  GameplayLuaError on any Lua, budget or binding error. */
        void invoke( const std::string &scriptId, const std::string &functionName,
                     const std::string &principal, const CommunicationEnvelope &envelope,
                     std::vector<CommunicationEnvelope> &replies );

        /** Builds a normal ActionRegistry Handler that delegates to
         *  GameplayLuaRuntime::invoke for the given binding. The handler
         *  captures a std::weak_ptr<GameplayLuaRuntime> - never a raw `this`
         *  - so a destroyed runtime leaves a safe no-op instead of a dangling
         *  pointer (provable lifetime). */
        static Handler bridgeHandler(
            std::weak_ptr<GameplayLuaRuntime> runtime, ScriptBinding binding );

        // Public only because the implementation file's free Lua trampolines
        // need the name; State's definition stays private to the .cpp.
        struct State;

    private:
        GameplayLuaRuntime( CommunicationRuntime &bus, DelayedMessageScheduler &scheduler,
                            const WorldState &world, std::uint64_t instructionBudget );
        ~GameplayLuaRuntime() noexcept;

        std::shared_ptr<State> mState;
        std::size_t mMaxInvocationDepth = 32;
        std::uint64_t mInstructionBudget = kDefaultInstructionBudget;
    };
} // namespace world::scripting
