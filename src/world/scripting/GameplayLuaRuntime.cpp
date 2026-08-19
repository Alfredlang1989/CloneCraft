#include "world/scripting/GameplayLuaRuntime.h"

#include "world/worldgen/LuaApi.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <thread>
#include <utility>

namespace world::scripting
{
    using namespace worldgen::lua54;

    namespace
    {
        using namespace worldgen::lua54;
        using world::BlockAddress;
        using world::RelativeI64;
        using namespace world::communication;
    } // namespace

    // -----------------------------------------------------------------------
    // Invocation context stack + instruction budget (defined before State:
    // State stores the stack, the budget hook reads the top context)
    // -----------------------------------------------------------------------

    /** Hook granularity: the count-hook fires every kBudgetStep Lua
     *  instructions; the per-invocation budget counts these batches. */
    constexpr std::uint64_t kBudgetStep = 250;

    struct InvocationContext
    {
        const CommunicationEnvelope *cause = nullptr;
        std::vector<CommunicationEnvelope> *replies = nullptr;
        std::string principal;
        std::uint64_t remainingInstructions = 0; // budget in instruction batches
        /** Set once the instruction budget was exceeded in THIS invocation:
         *  neither pcall nor xpcall may ever return the abort into script
         *  control (budget-aware sandbox wrappers re-raise it). */
        bool budgetExceeded = false;
    };

    // -----------------------------------------------------------------------
    // GameplayLuaRuntime::State (defined first: the Lua trampoline helpers
    // below need the complete type)
    // -----------------------------------------------------------------------

    struct GameplayLuaRuntime::State
    {
        State( CommunicationRuntime &b, DelayedMessageScheduler &s, const WorldState &w ) :
            bus( b ),
            scheduler( s ),
            world( w )
        {
        }

        ~State()
        {
            for( const auto &entry : envRefs )
                luaL_unref( L, REGISTRYINDEX, entry.second );
            if( globalEnvRef != NOREF )
                luaL_unref( L, REGISTRYINDEX, globalEnvRef );
            if( realPcallRef != NOREF )
                luaL_unref( L, REGISTRYINDEX, realPcallRef );
            if( realXpcallRef != NOREF )
                luaL_unref( L, REGISTRYINDEX, realXpcallRef );
            if( L )
                lua_close( L );
        }

        CommunicationRuntime &bus;
        DelayedMessageScheduler &scheduler;
        const WorldState &world;
        std::thread::id ownerThread = std::this_thread::get_id();
        lua_State *L = nullptr;
        int globalEnvRef = NOREF;
        int realPcallRef = NOREF;
        int realXpcallRef = NOREF;
        std::map<std::string, int> envRefs;
        std::vector<InvocationContext> invocationStack;
    };

    namespace
    {
        using namespace worldgen::lua54;
        using world::BlockAddress;
        using world::RelativeI64;
        using namespace world::communication;

        std::string kindString( EnvelopeKind kind )
        {
            switch( kind )
            {
                case EnvelopeKind::Command: return "Command";
                case EnvelopeKind::Query: return "Query";
                case EnvelopeKind::Event: return "Event";
                case EnvelopeKind::Reply: return "Reply";
            }
            return "<invalid>";
        }

        std::string luaErrorText( lua_State *L )
        {
            std::size_t len = 0;
            const char *message = lua_tolstring( L, -1, &len );
            return message ? std::string( message, len ) : std::string( "unknown Lua error" );
        }

        // -------------------------------------------------------------------
        // Instruction budget hook
        // -------------------------------------------------------------------

        // The Lua state is owner-thread only; a single thread-local pointer
        // to the currently active runtime is enough for the hook - it is
        // saved/restored by ThreadStateScope like the invocation stack.
        thread_local GameplayLuaRuntime::State *tRunningState = nullptr;

        class ThreadStateScope
        {
        public:
            explicit ThreadStateScope( GameplayLuaRuntime::State *state ) :
                mPrevious( tRunningState )
            {
                tRunningState = state;
            }
            ~ThreadStateScope() { tRunningState = mPrevious; }

        private:
            GameplayLuaRuntime::State *mPrevious = nullptr;
        };

        /** Private host sentinel for budget aborts. Lua can never produce
         *  this identity: it is a fixed internal address, so pcall/xpcall
         *  can never mistake a script-created value for it, and a budget
         *  abort can never be faked or swallowed into script control. */
        const void *budgetSentinelPtr()
        {
            static char budgetSentinel = 0;
            return static_cast<const void *>( &budgetSentinel );
        }

        bool isBudgetSentinel( lua_State *L, int idx )
        {
            return lua_type( L, idx ) == TLIGHTUSERDATA &&
                   lua_touserdata( L, idx ) == budgetSentinelPtr();
        }

        int raiseBudgetAbort( lua_State *L )
        {
            lua_pushlightuserdata( L,
                                   const_cast<void *>( budgetSentinelPtr() ) );
            return lua_error( L ); // never returns (longjmp into the outer pcall)
        }

        void instructionBudgetHook( lua_State *L, lua_Debug * )
        {
            GameplayLuaRuntime::State *state = tRunningState;
            if( !state || state->invocationStack.empty() )
                return;
            InvocationContext &ctx = state->invocationStack.back();
            if( ctx.remainingInstructions < kBudgetStep )
            {
                ctx.budgetExceeded = true;
                raiseBudgetAbort( L ); // uncatchable host abort; never returns
            }
            ctx.remainingInstructions -= kBudgetStep;
        }

        /** RAII stack balance: every C++ -> Lua crossing restores the stack
         *  top on ALL exit paths (success, Lua error, binding error, budget
         *  error, nested query). */
        class StackGuard
        {
        public:
            StackGuard( lua_State *L, int top ) : mL( L ), mTop( top ) {}
            ~StackGuard() { lua_settop( mL, mTop ); }
            StackGuard( const StackGuard & ) = delete;
            StackGuard &operator=( const StackGuard & ) = delete;

        private:
            lua_State *mL = nullptr;
            int mTop = 0;
        };

        void requireOwnerThread( const GameplayLuaRuntime::State &state )
        {
            if( std::this_thread::get_id() != state.ownerThread )
                throw GameplayLuaError(
                    "gameplay lua: the Lua runtime may only be used from its owner thread" );
        }

        const InvocationContext &requireHandlerContext( const GameplayLuaRuntime::State &state )
        {
            if( state.invocationStack.empty() )
                throw GameplayLuaError( "this bus API requires an active handler invocation" );
            const InvocationContext &ctx = state.invocationStack.back();
            if( !ctx.cause )
                throw GameplayLuaError(
                    "this bus API is not available during script top-level load" );
            return ctx;
        }

        GameplayLuaRuntime::State *stateFromUpvalue( lua_State *L )
        {
            return static_cast<GameplayLuaRuntime::State *>(
                lua_touserdata( L, luaUpvalueIndex( 1 ) ) );
        }

        // -------------------------------------------------------------------
        // strict Lua value readers
        // -------------------------------------------------------------------

        std::string requireString( lua_State *L, int idx, const char *what )
        {
            if( lua_type( L, idx ) != TSTRING )
                throw GameplayLuaError( std::string( what ) + " must be a string" );
            std::size_t len = 0;
            const char *s = lua_tolstring( L, idx, &len );
            return std::string( s ? s : "", len );
        }

        std::int64_t requireInt64( lua_State *L, int idx, const char *what )
        {
            int ok = 0;
            const lua_Integer value = lua_tointegerx( L, idx, &ok );
            if( !ok )
                throw GameplayLuaError( std::string( what ) + " must be an integer" );
            return static_cast<std::int64_t>( value );
        }

        /** Strict contract field reader: no truthy/falsy coercion - a
         *  boolean field must be a real Lua boolean. */
        bool requireBoolean( lua_State *L, int idx, const char *what )
        {
            if( lua_type( L, idx ) != TBOOLEAN )
                throw GameplayLuaError( std::string( what ) + " must be a boolean" );
            return lua_toboolean( L, idx ) != 0;
        }

        /** Wire/API contract tables must be PLAIN data tables: a metatable
         *  (with __index/__newindex/__pairs/...) could fabricate fields the
         *  strict codec would read via metamethods. Rejecting metatables is
         *  the unambiguous, simplest contract. Script-internal tables with
         *  metatables stay allowed - only the contract tables must be plain. */
        void requirePlainDataTable( lua_State *L, int idx, const char *what )
        {
            if( lua_type( L, idx ) != TTABLE )
                throw GameplayLuaError( std::string( what ) + " must be a table" );
            if( lua_getmetatable( L, idx ) != 0 )
            {
                lua_pop( L, 1 );
                throw GameplayLuaError( std::string( what ) +
                                        " must be a plain data table (no metatable)" );
            }
        }

        // -------------------------------------------------------------------
        // Envelope <-> Lua codecs (strict, explicit schema tags, no magic,
        // no silent type coercions, no silently ignored fields)
        // -------------------------------------------------------------------

        void wrapReadOnly( lua_State *L );

        world::PropertyValue decodePropertyValue( lua_State *L, int tableIdx, const char *what )
        {
            requirePlainDataTable( L, tableIdx, what );
            std::optional<std::string> valueType;
            std::optional<lua_Integer> integerValue; // set only for real integers
            std::optional<lua_Number> numberValue;   // set for any number
            lua_pushnil( L );
            while( lua_next( L, tableIdx ) != 0 )
            {
                const std::string key =
                    requireString( L, -2, ( std::string( what ) + " key" ).c_str() );
                if( key == "value_type" )
                    valueType = requireString( L, -1, ( std::string( what ) + ".value_type" ).c_str() );
                else if( key == "value" )
                {
                    if( lua_type( L, -1 ) != TNUMBER )
                        throw GameplayLuaError( std::string( what ) + ".value must be a number" );
                    numberValue = lua_tonumberx( L, -1, nullptr );
                    if( lua_isinteger( L, -1 ) )
                        integerValue = lua_tointegerx( L, -1, nullptr );
                }
                else
                    throw GameplayLuaError( std::string( what ) + ": unknown field '" + key + "'" );
                lua_pop( L, 1 );
            }
            if( !valueType || !numberValue )
                throw GameplayLuaError( std::string( what ) +
                                        " requires fields 'value_type' and 'value'" );
            if( *valueType == "u32" )
            {
                // strict: u32 requires a REAL integer value - a float like
                // 3.0 would be a silent type conversion (forbidden).
                if( !integerValue || *integerValue < 0 ||
                    *integerValue >
                        static_cast<lua_Integer>( std::numeric_limits<std::uint32_t>::max() ) )
                    throw GameplayLuaError( std::string( what ) +
                                            " with value_type 'u32' must be an integer value "
                                            "in uint32 range" );
                return world::PropertyValue{ static_cast<std::uint32_t>( *integerValue ) };
            }
            if( *valueType == "float" )
            {
                // A Lua number is a double while PropertyValue stores a
                // float. Reject non-finite and out-of-range values before
                // narrowing: neither Infinity/NaN nor a finite double that
                // overflows to float Infinity is valid transport data.
                constexpr lua_Number maxFloat =
                    static_cast<lua_Number>( std::numeric_limits<float>::max() );
                if( !std::isfinite( *numberValue ) || *numberValue < -maxFloat ||
                    *numberValue > maxFloat )
                    throw GameplayLuaError( std::string( what ) +
                                            " with value_type 'float' must be finite and "
                                            "within the float range" );
                return world::PropertyValue{ static_cast<float>( *numberValue ) };
            }
            throw GameplayLuaError( std::string( what ) + ".value_type must be 'u32' or 'float'" );
        }

        struct DecodedPayload
        {
            PayloadSchema schema = PayloadSchema::None;
            world::communication::Payload payload;
        };

        BlockAddress decodeBlockAddress( lua_State *L, int idx, const char *what );

        DecodedPayload decodePayload( lua_State *L, int idx )
        {
            if( lua_isnil( L, idx ) )
                return { PayloadSchema::None, Payload{} };
            requirePlainDataTable( L, idx, "payload" );
            lua_getfield( L, idx, "schema" );
            const std::string schema = requireString( L, -1, "payload.schema" );
            lua_pop( L, 1 );
            // Strict: iterate EVERY field - unknown fields are rejected and
            // fields that do not belong to the declared schema are rejected.
            std::optional<std::uint16_t> runtimeId;
            std::optional<std::string> property;
            std::optional<world::PropertyValue> eventValue;
            std::optional<BlockAddress> blockTarget;
            lua_pushnil( L );
            while( lua_next( L, idx ) != 0 )
            {
                const std::string key = requireString( L, -2, "payload key" );
                if( key == "schema" )
                {
                    if( lua_type( L, -1 ) != TSTRING )
                        throw GameplayLuaError( "payload.schema must be a string" );
                }
                else if( key == "runtime_id" )
                {
                    const std::int64_t rid = requireInt64( L, -1, "payload.runtime_id" );
                    if( rid < 0 ||
                        rid > static_cast<std::int64_t>( std::numeric_limits<std::uint16_t>::max() ) )
                        throw GameplayLuaError( "payload.runtime_id out of uint16 range" );
                    runtimeId = static_cast<std::uint16_t>( rid );
                }
                else if( key == "property" )
                    property = requireString( L, -1, "payload.property" );
                else if( key == "value" )
                    eventValue = decodePropertyValue( L, lua_absindex( L, -1 ), "payload.value" );
                else if( key == "target" )
                    blockTarget = decodeBlockAddress( L, lua_absindex( L, -1 ),
                                                      "payload.target" );
                else
                    throw GameplayLuaError( "payload: unknown field '" + key + "'" );
                lua_pop( L, 1 );
            }
            if( schema == "none" )
            {
                if( runtimeId || property || eventValue || blockTarget )
                    throw GameplayLuaError(
                        "payload.schema 'none' accepts no value fields" );
                return { PayloadSchema::None, Payload{} };
            }
            if( schema == "block_place" )
            {
                if( property || eventValue || blockTarget || !runtimeId )
                    throw GameplayLuaError(
                        "payload.schema 'block_place' requires exactly 'runtime_id'" );
                return { PayloadSchema::BlockPlace,
                         Payload{ BlockPlacePayload{ *runtimeId } } };
            }
            if( schema == "block_target" )
            {
                if( runtimeId || property || eventValue || !blockTarget )
                    throw GameplayLuaError(
                        "payload.schema 'block_target' requires exactly one 'target'" );
                return { PayloadSchema::BlockTarget,
                         Payload{ BlockTargetPayload{ *blockTarget } } };
            }
            if( schema == "property_set" )
            {
                if( runtimeId || blockTarget || !property || !eventValue )
                    throw GameplayLuaError(
                        "payload.schema 'property_set' requires exactly 'property' and 'value'" );
                return { PayloadSchema::PropertySet,
                         Payload{ PropertySetPayload{ *property, *eventValue } } };
            }
            if( schema == "query" )
            {
                if( runtimeId || eventValue || blockTarget || !property )
                    throw GameplayLuaError(
                        "payload.schema 'query' requires exactly one 'property'" );
                return { PayloadSchema::Query, Payload{ QueryPayload{ *property } } };
            }
            if( schema == "event_value" )
            {
                if( runtimeId || property || blockTarget || !eventValue )
                    throw GameplayLuaError(
                        "payload.schema 'event_value' requires exactly one 'value'" );
                return { PayloadSchema::EventValue, Payload{ EventValuePayload{ *eventValue } } };
            }
            if( schema == "command_result" )
                throw GameplayLuaError(
                    "payload.schema 'command_result' is only valid for bus.reply()" );
            throw GameplayLuaError( "unknown payload.schema '" + schema + "'" );
        }

        struct MessageSpec
        {
            std::string receiver;
            std::string context;
            std::string action;
            std::optional<std::string> capability;
            std::optional<std::string> replyTo;
            std::optional<BlockAddress> target;
            std::optional<EnvelopeKind> kind;
            bool causal = false;
            DecodedPayload payload;
        };

        BlockAddress decodeBlockAddress( lua_State *L, int idx, const char *what )
        {
            requirePlainDataTable( L, idx, what );
            std::optional<std::int64_t> x, y, z;
            lua_pushnil( L );
            while( lua_next( L, idx ) != 0 )
            {
                const std::string key =
                    requireString( L, -2, ( std::string( what ) + " key" ).c_str() );
                if( key == "x" )
                    x = requireInt64( L, -1, ( std::string( what ) + ".x" ).c_str() );
                else if( key == "y" )
                    y = requireInt64( L, -1, ( std::string( what ) + ".y" ).c_str() );
                else if( key == "z" )
                    z = requireInt64( L, -1, ( std::string( what ) + ".z" ).c_str() );
                else
                    throw GameplayLuaError( std::string( what ) + ": unknown field '" + key + "'" );
                lua_pop( L, 1 );
            }
            if( !x || !y || !z )
                throw GameplayLuaError( std::string( what ) +
                                        " requires the fields x, y and z" );
            return world::fromOriginOffset( *x, *y, *z );
        }

        /** Strict spec decoder: UNKNOWN FIELDS ARE REJECTED - Lua can never
         *  smuggle `sender`, `message_id`, `correlation_id` or any other
         *  authoritative field into an outbound message. */
        MessageSpec decodeMessageSpec( lua_State *L, int idx )
        {
            requirePlainDataTable( L, idx, "message spec" );
            MessageSpec spec;
            lua_pushnil( L );
            while( lua_next( L, idx ) != 0 )
            {
                const std::string key = requireString( L, -2, "message spec key" );
                if( key == "receiver" )
                    spec.receiver = requireString( L, -1, "spec.receiver" );
                else if( key == "context" )
                    spec.context = requireString( L, -1, "spec.context" );
                else if( key == "action" )
                    spec.action = requireString( L, -1, "spec.action" );
                else if( key == "capability" )
                {
                    if( !lua_isnil( L, -1 ) )
                        spec.capability = requireString( L, -1, "spec.capability" );
                }
                else if( key == "reply_to" )
                {
                    if( !lua_isnil( L, -1 ) )
                        spec.replyTo = requireString( L, -1, "spec.reply_to" );
                }
                else if( key == "target" )
                    spec.target = decodeBlockAddress( L, lua_absindex( L, -1 ), "spec.target" );
                else if( key == "causal" )
                    spec.causal = requireBoolean( L, -1, "spec.causal" );
                else if( key == "kind" )
                {
                    const std::string k = requireString( L, -1, "spec.kind" );
                    if( k == "command" )
                        spec.kind = EnvelopeKind::Command;
                    else if( k == "event" )
                        spec.kind = EnvelopeKind::Event;
                    else if( k == "query" )
                        spec.kind = EnvelopeKind::Query;
                    else
                        throw GameplayLuaError( "spec.kind must be 'command', 'event' or 'query'" );
                }
                else if( key == "payload" )
                    spec.payload = decodePayload( L, lua_absindex( L, -1 ) );
                else
                    throw GameplayLuaError( "unknown field '" + key + "' in message spec" );
                lua_pop( L, 1 );
            }
            if( spec.receiver.empty() )
                throw GameplayLuaError( "spec.receiver must not be empty" );
            if( spec.context.empty() )
                throw GameplayLuaError( "spec.context must not be empty" );
            if( spec.action.empty() )
                throw GameplayLuaError( "spec.action must not be empty" );
            return spec;
        }

        CommunicationEnvelope buildEnvelope( const GameplayLuaRuntime::State &,
                                             const InvocationContext &ctx, EnvelopeKind kind,
                                             const MessageSpec &spec )
        {
            CommunicationEnvelope env;
            env.kind = kind;
            env.sender = ctx.principal; // HOST-assigned - Lua can never spoof it
            env.receiver = spec.receiver;
            env.context = spec.context;
            env.action = spec.action;
            if( spec.capability )
                env.capability = spec.capability;
            if( spec.replyTo )
                env.replyTo = spec.replyTo;
            if( spec.target )
                env.target = WorldStateTarget( *spec.target );
            env.payload = spec.payload.payload;
            // Optional causal correlation: only the CURRENT cause's real
            // messageId may be linked - Lua can never choose a raw number.
            if( spec.causal && ctx.cause )
                env.correlationId = ctx.cause->messageId;
            return env;
        }

        void pushPropertyValue( lua_State *L, const world::PropertyValue &value )
        {
            lua_createtable( L, 0, 2 );
            if( std::holds_alternative<std::uint32_t>( value ) )
            {
                lua_pushstring( L, "u32" );
                lua_setfield( L, -2, "value_type" );
                lua_pushinteger( L, static_cast<lua_Integer>( std::get<std::uint32_t>( value ) ) );
                lua_setfield( L, -2, "value" );
            }
            else
            {
                lua_pushstring( L, "float" );
                lua_setfield( L, -2, "value_type" );
                lua_pushnumber( L, static_cast<lua_Number>( std::get<float>( value ) ) );
                lua_setfield( L, -2, "value" );
            }
            wrapReadOnly( L );
        }

        void pushBlockAddressSnapshot( lua_State *L, const BlockAddress &address,
                                       const char *what )
        {
            RelativeI64 rel{};
            if( !world::blockDeltaWithin( address, world::originBlockAddress(),
                                          std::numeric_limits<std::int64_t>::max() / 2, rel ) )
                throw GameplayLuaError( std::string( what ) +
                                        " is outside the Lua origin-offset range" );
            lua_createtable( L, 0, 3 );
            lua_pushinteger( L, rel.x );
            lua_setfield( L, -2, "x" );
            lua_pushinteger( L, rel.y );
            lua_setfield( L, -2, "y" );
            lua_pushinteger( L, rel.z );
            lua_setfield( L, -2, "z" );
            wrapReadOnly( L );
        }

        void pushPayloadSnapshot( lua_State *L, const Payload &payload )
        {
            if( std::holds_alternative<std::monostate>( payload ) )
            {
                lua_pushnil( L );
                return;
            }
            lua_createtable( L, 0, 4 );
            if( std::holds_alternative<BlockPlacePayload>( payload ) )
            {
                const auto &p = std::get<BlockPlacePayload>( payload );
                lua_pushstring( L, "block_place" );
                lua_setfield( L, -2, "schema" );
                lua_pushinteger( L, static_cast<lua_Integer>( p.runtimeId ) );
                lua_setfield( L, -2, "runtime_id" );
            }
            else if( std::holds_alternative<BlockTargetPayload>( payload ) )
            {
                const auto &p = std::get<BlockTargetPayload>( payload );
                lua_pushstring( L, "block_target" );
                lua_setfield( L, -2, "schema" );
                pushBlockAddressSnapshot( L, p.target, "payload target" );
                lua_setfield( L, -2, "target" );
            }
            else if( std::holds_alternative<PropertySetPayload>( payload ) )
            {
                const auto &p = std::get<PropertySetPayload>( payload );
                lua_pushstring( L, "property_set" );
                lua_setfield( L, -2, "schema" );
                lua_pushlstring( L, p.property.data(), p.property.size() );
                lua_setfield( L, -2, "property" );
                pushPropertyValue( L, p.value );
                lua_setfield( L, -2, "value" );
            }
            else if( std::holds_alternative<CommandResultPayload>( payload ) )
            {
                const auto &p = std::get<CommandResultPayload>( payload );
                lua_pushstring( L, "command_result" );
                lua_setfield( L, -2, "schema" );
                lua_pushboolean( L, p.ok ? 1 : 0 );
                lua_setfield( L, -2, "ok" );
                lua_pushlstring( L, p.error.data(), p.error.size() );
                lua_setfield( L, -2, "error" );
                if( p.value )
                    pushPropertyValue( L, *p.value );
                else
                    lua_pushnil( L );
                lua_setfield( L, -2, "value" );
            }
            else if( std::holds_alternative<QueryPayload>( payload ) )
            {
                const auto &p = std::get<QueryPayload>( payload );
                lua_pushstring( L, "query" );
                lua_setfield( L, -2, "schema" );
                lua_pushlstring( L, p.property.data(), p.property.size() );
                lua_setfield( L, -2, "property" );
            }
            else if( std::holds_alternative<EventValuePayload>( payload ) )
            {
                const auto &p = std::get<EventValuePayload>( payload );
                lua_pushstring( L, "event_value" );
                lua_setfield( L, -2, "schema" );
                pushPropertyValue( L, p.value );
                lua_setfield( L, -2, "value" );
            }
            wrapReadOnly( L );
        }

        void pushEnvelopeSnapshot( lua_State *L, const CommunicationEnvelope &env )
        {
            // messageId/correlationId are FULL uint64_t: exposed to Lua as
            // opaque DECIMAL STRINGS (Lua's signed int64 would wrap/round
            // ids >= 2^63 - no double conversion, no precision loss).
            const std::string idText = std::to_string( env.messageId );
            lua_createtable( L, 0, 12 );
            lua_pushlstring( L, idText.data(), idText.size() );
            lua_setfield( L, -2, "message_id" );
            lua_pushlstring( L, kindString( env.kind ).data(), kindString( env.kind ).size() );
            lua_setfield( L, -2, "kind" );
            lua_pushlstring( L, env.sender.data(), env.sender.size() );
            lua_setfield( L, -2, "sender" );
            lua_pushlstring( L, env.receiver.data(), env.receiver.size() );
            lua_setfield( L, -2, "receiver" );
            lua_pushlstring( L, env.context.data(), env.context.size() );
            lua_setfield( L, -2, "context" );
            lua_pushlstring( L, env.action.data(), env.action.size() );
            lua_setfield( L, -2, "action" );
            if( env.capability )
                lua_pushstring( L, env.capability->c_str() );
            else
                lua_pushnil( L );
            lua_setfield( L, -2, "capability" );
            if( env.target && std::holds_alternative<BlockAddress>( env.target->value() ) )
            {
                const BlockAddress &addr = std::get<BlockAddress>( env.target->value() );
                RelativeI64 rel{};
                if( world::blockDeltaWithin( addr, world::originBlockAddress(),
                                             std::numeric_limits<std::int64_t>::max() / 2, rel ) )
                {
                    lua_createtable( L, 0, 3 );
                    lua_pushinteger( L, rel.x );
                    lua_setfield( L, -2, "x" );
                    lua_pushinteger( L, rel.y );
                    lua_setfield( L, -2, "y" );
                    lua_pushinteger( L, rel.z );
                    lua_setfield( L, -2, "z" );
                    wrapReadOnly( L ); // target is part of the read-only snapshot
                }
                else
                    lua_pushnil( L );
            }
            else
                lua_pushnil( L );
            lua_setfield( L, -2, "target" );
            if( env.replyTo )
                lua_pushstring( L, env.replyTo->c_str() );
            else
                lua_pushnil( L );
            lua_setfield( L, -2, "reply_to" );
            if( env.correlationId )
            {
                const std::string corrText = std::to_string( *env.correlationId );
                lua_pushlstring( L, corrText.data(), corrText.size() );
            }
            else
                lua_pushnil( L );
            lua_setfield( L, -2, "correlation_id" );
            pushPayloadSnapshot( L, env.payload );
            lua_setfield( L, -2, "payload" );
            // Read-only proxy: reads go through __index to the hidden data
            // table, EVERY write (new or existing key) is rejected by
            // __newindex, and the metatable itself is protected.
            wrapReadOnly( L );
        }

        CommandResultPayload decodeReplyPayload( lua_State *L, int idx )
        {
            requirePlainDataTable( L, idx, "bus.reply payload" );
            CommandResultPayload result;
            std::optional<std::string> schema;
            std::optional<bool> ok;
            std::optional<std::string> error;
            std::optional<world::PropertyValue> value;
            lua_pushnil( L );
            while( lua_next( L, idx ) != 0 )
            {
                const std::string key = requireString( L, -2, "reply payload key" );
                if( key == "schema" )
                    schema = requireString( L, -1, "reply payload.schema" );
                else if( key == "ok" )
                    ok = requireBoolean( L, -1, "reply payload.ok" );
                else if( key == "error" )
                {
                    if( lua_type( L, -1 ) != TSTRING )
                        throw GameplayLuaError( "reply payload.error must be a string" );
                    error = requireString( L, -1, "reply payload.error" );
                }
                else if( key == "value" )
                    value = decodePropertyValue( L, lua_absindex( L, -1 ), "reply payload.value" );
                else
                    throw GameplayLuaError( "reply payload: unknown field '" + key + "'" );
                lua_pop( L, 1 );
            }
            if( schema && *schema != "command_result" )
                throw GameplayLuaError( "reply payload.schema must be 'command_result'" );
            result.ok = ok.value_or( false );
            result.error = error.value_or( "" );
            result.value = value;
            return result;
        }

        // -------------------------------------------------------------------
        // Lua bus / world bindings (tight C-function trampolines; every C++
        // exception becomes a Lua error; no raw pointers reach Lua)
        // -------------------------------------------------------------------

        int luaBusSend( lua_State *L )
        {
            GameplayLuaRuntime::State *state = stateFromUpvalue( L );
            try
            {
                requireOwnerThread( *state );
                const InvocationContext &ctx = requireHandlerContext( *state );
                MessageSpec spec = decodeMessageSpec( L, 1 );
                const EnvelopeKind kind = spec.kind.value_or( EnvelopeKind::Event );
                if( kind != EnvelopeKind::Command && kind != EnvelopeKind::Event )
                    throw GameplayLuaError( "bus.send only supports kind 'command' or 'event'" );
                CommunicationEnvelope env = buildEnvelope( *state, ctx, kind, spec );
                env.messageId = state->bus.nextMessageId();
                if( !state->bus.submit( env ) )
                    throw GameplayLuaError(
                        "bus.send: the inbound bus queue is full (backpressure) - the "
                        "message was NOT queued and NOT dropped; retry later or schedule it" );
                lua_pushboolean( L, 1 );
                return 1;
            }
            catch( const GameplayLuaBudgetError & )
            {
                // Budget abort: re-raise the private host sentinel - neither
                // pcall nor xpcall may ever return it into script control.
                return raiseBudgetAbort( L ); // never returns
            }
            catch( const std::exception &error )
            {
                return luaL_error( L, "%s", error.what() );
            }
        }

        int luaBusQuery( lua_State *L )
        {
            GameplayLuaRuntime::State *state = stateFromUpvalue( L );
            try
            {
                requireOwnerThread( *state );
                const InvocationContext &ctx = requireHandlerContext( *state );
                MessageSpec spec = decodeMessageSpec( L, 1 );
                if( spec.kind && *spec.kind != EnvelopeKind::Query )
                    throw GameplayLuaError( "bus.query spec.kind must be 'query'" );
                CommunicationEnvelope env = buildEnvelope( *state, ctx, EnvelopeKind::Query, spec );
                env.messageId = state->bus.nextMessageId();
                const CommunicationRouter::DispatchResult result = state->bus.dispatch( env );
                lua_createtable( L, static_cast<int>( result.replies.size() ), 0 );
                int index = 1;
                for( const CommunicationEnvelope &reply : result.replies )
                {
                    pushEnvelopeSnapshot( L, reply );
                    lua_rawseti( L, -2, index++ );
                }
                return 1;
            }
            catch( const GameplayLuaBudgetError & )
            {
                // Budget abort: re-raise the private host sentinel - neither
                // pcall nor xpcall may ever return it into script control.
                return raiseBudgetAbort( L ); // never returns
            }
            catch( const std::exception &error )
            {
                return luaL_error( L, "%s", error.what() );
            }
        }

        int luaBusReply( lua_State *L )
        {
            GameplayLuaRuntime::State *state = stateFromUpvalue( L );
            try
            {
                requireOwnerThread( *state );
                const InvocationContext &ctx = requireHandlerContext( *state );
                CommandResultPayload result = decodeReplyPayload( L, 1 );
                CommunicationEnvelope reply =
                    state->bus.makeReply( *ctx.cause, std::move( result ) );
                ctx.replies->push_back( std::move( reply ) );
                return 0;
            }
            catch( const GameplayLuaBudgetError & )
            {
                // Budget abort: re-raise the private host sentinel - neither
                // pcall nor xpcall may ever return it into script control.
                return raiseBudgetAbort( L ); // never returns
            }
            catch( const std::exception &error )
            {
                return luaL_error( L, "%s", error.what() );
            }
        }

        int luaBusScheduleAfter( lua_State *L )
        {
            GameplayLuaRuntime::State *state = stateFromUpvalue( L );
            try
            {
                requireOwnerThread( *state );
                const InvocationContext &ctx = requireHandlerContext( *state );
                const std::int64_t delayMs = requireInt64( L, 1, "delay_ms" );
                if( delayMs < 0 )
                    throw GameplayLuaError( "schedule_after_ms: delay_ms must be >= 0" );
                using Ms = std::chrono::milliseconds;
                const std::int64_t maxDelayMs =
                    std::chrono::duration_cast<Ms>(
                        std::chrono::steady_clock::duration::max() )
                        .count();
                if( delayMs > maxDelayMs )
                    throw GameplayLuaError(
                        "schedule_after_ms: delay_ms overflows the clock domain" );
                MessageSpec spec = decodeMessageSpec( L, 2 );
                const EnvelopeKind kind = spec.kind.value_or( EnvelopeKind::Event );
                if( kind != EnvelopeKind::Command && kind != EnvelopeKind::Event )
                    throw GameplayLuaError(
                        "schedule_after_ms only supports kind 'command' or 'event'" );
                CommunicationEnvelope env = buildEnvelope( *state, ctx, kind, spec );
                // Id minted AT SCHEDULING; the scheduler transports exactly
                // this envelope and never re-issues the id at due time.
                env.messageId = state->bus.nextMessageId();
                state->scheduler.scheduleAfter( Ms( delayMs ), std::move( env ) );
                return 0;
            }
            catch( const GameplayLuaBudgetError & )
            {
                // Budget abort: re-raise the private host sentinel - neither
                // pcall nor xpcall may ever return it into script control.
                return raiseBudgetAbort( L ); // never returns
            }
            catch( const std::exception &error )
            {
                return luaL_error( L, "%s", error.what() );
            }
        }

        int luaWorldGetBlock( lua_State *L )
        {
            GameplayLuaRuntime::State *state = stateFromUpvalue( L );
            try
            {
                requireOwnerThread( *state );
                const BlockAddress addr = decodeBlockAddress( L, 1, "world.get_block target" );
                const std::optional<std::uint16_t> id = state->world.blockAt( addr );
                lua_createtable( L, 0, 2 );
                if( id )
                {
                    lua_pushboolean( L, 1 );
                    lua_setfield( L, -2, "loaded" );
                    lua_pushinteger( L, static_cast<lua_Integer>( *id ) );
                    lua_setfield( L, -2, "block_id" );
                }
                else
                {
                    lua_pushboolean( L, 0 );
                    lua_setfield( L, -2, "loaded" );
                    lua_pushnil( L );
                    lua_setfield( L, -2, "block_id" );
                }
                return 1;
            }
            catch( const GameplayLuaBudgetError & )
            {
                // Budget abort: re-raise the private host sentinel - neither
                // pcall nor xpcall may ever return it into script control.
                return raiseBudgetAbort( L ); // never returns
            }
            catch( const std::exception &error )
            {
                return luaL_error( L, "%s", error.what() );
            }
        }

        int luaReadOnlyNewIndex( lua_State *L )
        {
            return luaL_error( L, "gameplay lua: message snapshots are read-only" );
        }

        /** Replaces the table on top of the stack with a read-only proxy:
         *  the original data table becomes unreachable (only referenced by
         *  the proxy metatable's __index); every read is served from it and
         *  every write - new OR existing key - is rejected by __newindex.
         *  The metatable is protected via __metatable. */
        void wrapReadOnly( lua_State *L )
        {
            const int dataIdx = lua_gettop( L );
            lua_createtable( L, 0, 0 );                  // [data, proxy]
            lua_createtable( L, 0, 3 );                  // [data, proxy, mt]
            lua_pushvalue( L, dataIdx );                 // [data, proxy, mt, data]
            lua_setfield( L, -2, "__index" );            // [data, proxy, mt]
            lua_pushcfunction( L, luaReadOnlyNewIndex ); // [data, proxy, mt, fn]
            lua_setfield( L, -2, "__newindex" );         // [data, proxy, mt]
            lua_pushstring( L, "gameplay message snapshot is read-only" );
            lua_setfield( L, -2, "__metatable" );        // [data, proxy, mt]
            lua_setmetatable( L, -2 );                   // [data, proxy] (mt popped)
            lua_remove( L, dataIdx );                    // [proxy]
        }

        /** Shallow table copy (values are shared references; the table
         *  OBJECT itself is new). Used for the per-script sandbox namespace
         *  isolation in loadScript. Both indices are converted to absolute
         *  indices first: the stack grows inside the iteration. */
        void copyTableShallow( lua_State *L, int srcIdx, int dstIdx )
        {
            srcIdx = lua_absindex( L, srcIdx );
            dstIdx = lua_absindex( L, dstIdx );
            lua_pushnil( L );
            while( lua_next( L, srcIdx ) != 0 )
            {
                lua_pushvalue( L, -2 ); // key
                lua_pushvalue( L, -2 ); // value
                lua_rawset( L, dstIdx );
                lua_pop( L, 1 ); // value (key stays for lua_next)
            }
        }

        /** Common tail of the budget-aware pcall/xpcall wrappers: after the
         *  real protected call returned, a budget abort (flag set by the
         *  hook OR the private sentinel as the caught error object) must be
         *  re-raised - it can never be handed to script control. */
        bool budgetAbortPending( lua_State *L, GameplayLuaRuntime::State &state )
        {
            if( !state.invocationStack.empty() && state.invocationStack.back().budgetExceeded )
                return true;
            return lua_gettop( L ) > 0 && isBudgetSentinel( L, -1 );
        }

        /** Budget-aware replacement for the stock pcall: normal Lua errors
         *  are caught exactly like before, but an instruction-budget abort is
         *  re-raised as the private host sentinel until the outer host
         *  invoke() receives it as a GameplayLuaError. */
        int luaSafePcall( lua_State *L )
        {
            GameplayLuaRuntime::State *state = static_cast<GameplayLuaRuntime::State *>(
                lua_touserdata( L, luaUpvalueIndex( 2 ) ) );
            // Stock pcall accepts a missing/non-callable first value and
            // reports the attempted call as its protected `(false, error)`
            // result. lua_pcallk still needs an actual function slot, so
            // materialize the omitted value as nil instead of passing the
            // invalid nargs=-1 shape to the C API.
            if( lua_gettop( L ) == 0 )
                lua_pushnil( L );
            // lua_pcallk's nargs counts the arguments AFTER the target
            // function: the target sits in ONE of our own args (gettop-1).
            const int nargs = lua_gettop( L ) - 1;
            const int status = lua_pcallk( L, nargs, MULTRET, 0, 0, nullptr );
            if( status != OK && budgetAbortPending( L, *state ) )
                return raiseBudgetAbort( L ); // never returns
            // Stock pcall result shape: (true, results...) / (false, error).
            lua_pushboolean( L, status == OK ? 1 : 0 );
            lua_rotate( L, 1, 1 ); // move the boolean below the results
            return lua_gettop( L );
        }

        /** Raises the captured normal xpcall error inside a second protected
         *  call. This lets the script handler be lua_pcallk's real errfunc
         *  (rather than an ordinary called function), preserving Lua's exact
         *  recursive message-handler-failure behavior. */
        int luaRaiseCapturedError( lua_State *L )
        {
            lua_pushvalue( L, luaUpvalueIndex( 1 ) );
            return lua_error( L ); // never returns
        }

        /** Real xpcall message-handler guard.
         *
         * The first handler execution is protected WITHOUT an errfunc. A
         * handler-local budget abort can therefore be converted directly into
         * the private sentinel result consumed by luaSafeXpcall, instead of
         * being recursively delivered to the script handler. For an ordinary
         * handler failure, the original handler is then installed as the real
         * errfunc for the captured failure, preserving Lua 5.4's recursive
         * handler behavior and eventual LUA_ERRERR object. */
        int luaGuardedMessageHandler( lua_State *L )
        {
            GameplayLuaRuntime::State *state = static_cast<GameplayLuaRuntime::State *>(
                lua_touserdata( L, luaUpvalueIndex( 2 ) ) );

            // Two handler applications compensate each recursively retained
            // guard/raiser pair. The outermost guard performs two additional
            // applications to compensate the outer guard/raiser pair too;
            // this retains stock Lua 5.4's exact observable recursion count.
            const int applications = lua_toboolean( L, luaUpvalueIndex( 3 ) ) ? 4 : 2;
            for( int i = 0; i < applications; ++i )
            {
                const int errorIdx = lua_gettop( L );
                lua_pushvalue( L, luaUpvalueIndex( 1 ) );
                lua_pushvalue( L, errorIdx );
                const int status = lua_pcallk( L, 1, 1, 0, 0, nullptr );
                if( status == OK )
                    return 1; // exactly one handler result
                if( budgetAbortPending( L, *state ) )
                {
                    // Return, do not raise: this guard is itself lua_pcallk's
                    // errfunc. Raising here would recursively invoke it. The
                    // outer wrapper consumes this private result and re-raises
                    // before script control resumes.
                    lua_pushlightuserdata( L,
                                           const_cast<void *>( budgetSentinelPtr() ) );
                    return 1;
                }
            }

            // Continue recursion through another guard. Each nested guard
            // performs two sequential protected handler applications, which
            // compensates its two retained C frames and preserves stock Lua
            // 5.4's observable recursion count. Crucially, EVERY recursive
            // handler execution remains budget-guarded, not just the first.
            lua_pushvalue( L, -1 ); // capture the second handler error
            lua_pushcclosure( L, luaRaiseCapturedError, 1 ); // [..., raiser]
            lua_pushvalue( L, luaUpvalueIndex( 1 ) );        // [..., raiser, handler]
            lua_pushlightuserdata( L, state );               // [..., raiser, handler, state]
            lua_pushboolean( L, 0 );                         // nested guard
            lua_pushcclosure( L, luaGuardedMessageHandler, 3 ); // [..., raiser, guard]
            const int errfunc = lua_gettop( L );
            lua_rotate( L, errfunc - 1, 1 ); // [..., guard, raiser]
            (void)lua_pcallk( L, 0, 1, errfunc - 1, 0, nullptr );
            return 1; // handler result or "error in error handling"
        }

        /** Budget-aware replacement for the stock xpcall with REAL Lua 5.4
         *  semantics: f(args...) is protected WITHOUT the script error
         *  handler as errfunc - the handler can therefore never see the
         *  private budget sentinel. A budget abort re-raises immediately
         *  (the handler is NOT executed at all); only NORMAL Lua errors run
         *  the handler, separately and also protected. The handler itself is
         *  never part of the returned results. */
        int luaSafeXpcall( lua_State *L )
        {
            GameplayLuaRuntime::State *state = static_cast<GameplayLuaRuntime::State *>(
                lua_touserdata( L, luaUpvalueIndex( 2 ) ) );
            const int n = lua_gettop( L );
            // Stock Lua 5.4 accepts any first value (attempting to call a
            // non-function becomes the protected error result) but requires
            // the second argument to be an error-handler function.
            if( n < 2 || lua_type( L, 2 ) != TFUNCTION )
                return luaL_error( L,
                                   "gameplay lua: xpcall expects (value, error handler, ...)" );
            const int nargs = n - 2; // arguments after the handler

            // Reorder [f, handler, a1..ak] -> [handler, f, a1..ak]:
            // f must sit DIRECTLY below its arguments for lua_pcallk
            // (func at top-(nargs+1)); the handler stays at index 1 and is
            // deliberately NOT passed as the errfunc.
            lua_pushvalue( L, 1 ); // [f, handler, args..., f]
            lua_remove( L, 1 );    // [handler, args..., f]
            lua_rotate( L, 2, 1 ); // [handler, f, args...]

            // Step 1: protected call of f(args...) - no script handler.
            const int status = lua_pcallk( L, nargs, MULTRET, 0, 0, nullptr );
            if( status == OK )
            {
                // (true, results...) - the handler frame must not leak into
                // the returned results.
                lua_remove( L, 1 ); // [results...]
                lua_pushboolean( L, 1 );
                lua_rotate( L, 1, 1 );
                return lua_gettop( L );
            }

            // Error path. Step 2: budget FIRST - re-raise the private
            // sentinel immediately. The script error handler is NOT run and
            // the sentinel never reaches Lua.
            if( budgetAbortPending( L, *state ) )
                return raiseBudgetAbort( L ); // never returns

            // Step 3: normal Lua error - run it through a real guarded errfunc.
            // The guard catches a handler-local budget abort without recursive
            // delivery, while ordinary handler failures recurse through the
            // original Lua handler with stock behavior.
            lua_pushvalue( L, -1 ); // [handler, err, err(captured)]
            lua_pushcclosure( L, luaRaiseCapturedError, 1 ); // [handler, err, raiser]
            lua_remove( L, 2 );                            // [handler, raiser]
            lua_pushvalue( L, 1 );                         // [handler, raiser, handler]
            lua_pushlightuserdata( L, state );             // [handler, raiser, handler, state]
            lua_pushboolean( L, 1 );                       // outermost guard
            lua_pushcclosure( L, luaGuardedMessageHandler, 3 ); // [handler, raiser, guard]
            lua_remove( L, 1 );                            // [raiser, guard]
            lua_rotate( L, 1, 1 );                         // [guard, raiser]
            // A Lua message handler produces exactly ONE replacement error
            // object; additional return values are discarded by stock
            // xpcall semantics. The guard at index 1 is the REAL errfunc for
            // the captured-error raiser; it delegates ordinary recursive
            // failures to the original handler and eventually
            // produces LUA_ERRERR / "error in error handling", including the
            // stock observable handler side effects and recursion count.
            const int hstatus = lua_pcallk( L, 0, 1, 1, 0, nullptr );
            if( hstatus != OK )
            {
                if( budgetAbortPending( L, *state ) )
                    return raiseBudgetAbort( L ); // handler budget aborted
            }
            // Remove the retained errfunc; lua_pcallk left the handler result
            // (or Lua's own LUA_ERRERR object) above it.
            lua_remove( L, 1 );
            // Step 4: (false, handlerResult).
            lua_pushboolean( L, 0 );
            lua_rotate( L, 1, 1 );
            return lua_gettop( L );
        }

        // -------------------------------------------------------------------
        // state construction / sandbox
        // -------------------------------------------------------------------

        void buildControlledEnvironment( lua_State *L, GameplayLuaRuntime::State &state )
        {
            StackGuard guard( L, lua_gettop( L ) );

            luaL_openlibs( L );
            // Remove everything outside the controlled Round-3 surface before
            // any script can see it.
            for( const char *name :
                 { "os", "io", "debug", "package", "require", "dofile", "loadfile", "load",
                   "coroutine" } )
            {
                lua_pushnil( L );
                lua_setglobal( L, name );
            }
            // No uncontrolled randomness in Round 3.
            lua_getglobal( L, "math" );
            lua_pushnil( L );
            lua_setfield( L, -2, "random" );
            lua_pushnil( L );
            lua_setfield( L, -2, "randomseed" );
            lua_pop( L, 1 );

            // Capture the REAL pcall/xpcall privately: the sandbox exposes
            // budget-aware wrappers instead, so an instruction-budget abort
            // can never be returned into script control. Lua never sees
            // these refs (registry refs are invisible to scripts).
            lua_getglobal( L, "pcall" );
            state.realPcallRef = luaL_ref( L, REGISTRYINDEX );
            lua_getglobal( L, "xpcall" );
            state.realXpcallRef = luaL_ref( L, REGISTRYINDEX );

            // Controlled global environment: scripts index it through their
            // _ENV metatable; they never see the real _G. pcall/xpcall are
            // registered as budget-aware wrappers below; rawset/rawget are
            // NOT part of the sandbox (they would bypass the read-only
            // snapshot proxies) and neither is getmetatable (it would expose
            // the shared host-/type-global metatables, e.g. the string-type
            // metatable, breaking script isolation). setmetatable stays so
            // scripts can build their own table patterns.
            lua_createtable( L, 0, 8 );
            const int envIdx = lua_gettop( L );
            for( const char *name :
                 { "assert", "error", "ipairs", "pairs", "next", "select", "tonumber",
                   "tostring", "type", "rawequal", "setmetatable", "print" } )
            {
                lua_getglobal( L, name );
                lua_setfield( L, envIdx, name );
            }
            // Master namespace tables: per-script shallow copies are made at
            // loadScript time - the table OBJECTS are never shared mutably
            // between scripts (the C functions inside are shared).
            for( const char *name : { "table", "string", "math", "utf8" } )
            {
                lua_getglobal( L, name );
                lua_setfield( L, envIdx, name );
            }

            // Budget-aware pcall/xpcall (upvalues: real function, State*).
            lua_rawgeti( L, REGISTRYINDEX, state.realPcallRef );
            lua_pushlightuserdata( L, &state );
            lua_pushcclosure( L, luaSafePcall, 2 );
            lua_setfield( L, envIdx, "pcall" );
            lua_rawgeti( L, REGISTRYINDEX, state.realXpcallRef );
            lua_pushlightuserdata( L, &state );
            lua_pushcclosure( L, luaSafeXpcall, 2 );
            lua_setfield( L, envIdx, "xpcall" );

            // bus.* bindings.
            lua_createtable( L, 0, 4 );
            {
                const int busIdx = lua_gettop( L );
                lua_pushlightuserdata( L, &state );
                lua_pushcclosure( L, luaBusSend, 1 );
                lua_setfield( L, busIdx, "send" );
                lua_pushlightuserdata( L, &state );
                lua_pushcclosure( L, luaBusQuery, 1 );
                lua_setfield( L, busIdx, "query" );
                lua_pushlightuserdata( L, &state );
                lua_pushcclosure( L, luaBusReply, 1 );
                lua_setfield( L, busIdx, "reply" );
                lua_pushlightuserdata( L, &state );
                lua_pushcclosure( L, luaBusScheduleAfter, 1 );
                lua_setfield( L, busIdx, "schedule_after_ms" );
            }
            lua_pushvalue( L, -1 );
            lua_setfield( L, envIdx, "bus" );
            lua_pop( L, 1 );

            // world.* bindings - read-only view.
            lua_createtable( L, 0, 1 );
            {
                const int worldIdx = lua_gettop( L );
                lua_pushlightuserdata( L, &state );
                lua_pushcclosure( L, luaWorldGetBlock, 1 );
                lua_setfield( L, worldIdx, "get_block" );
            }
            lua_pushvalue( L, -1 );
            lua_setfield( L, envIdx, "world" );
            lua_pop( L, 1 );

            // Publish the controlled env for the per-script metatables.
            lua_pushvalue( L, envIdx );
            state.globalEnvRef = luaL_ref( L, REGISTRYINDEX );
        }
    } // namespace

    GameplayLuaRuntime::GameplayLuaRuntime( CommunicationRuntime &bus,
                                            DelayedMessageScheduler &scheduler,
                                            const WorldState &world,
                                            std::uint64_t instructionBudget ) :
        mInstructionBudget( instructionBudget )
    {
        mState = std::make_shared<State>( bus, scheduler, world );
        mState->L = luaL_newstate();
        if( !mState->L )
            throw GameplayLuaError( "gameplay lua: luaL_newstate failed" );
        buildControlledEnvironment( mState->L, *mState );
        // Install the instruction-budget count hook EXACTLY ONCE per state:
        // loadScript/invoke/nested invocations never re-arm it, so a nested
        // query can never reset the budget countdown (Round-3 hardening).
        lua_sethook( mState->L, instructionBudgetHook, MASKCOUNT,
                     static_cast<int>( kBudgetStep ) );
    }

    std::shared_ptr<GameplayLuaRuntime> GameplayLuaRuntime::create(
        CommunicationRuntime &bus, DelayedMessageScheduler &scheduler,
        const WorldState &world, std::uint64_t instructionBudget )
    {
        GameplayLuaRuntime *runtime =
            new GameplayLuaRuntime( bus, scheduler, world, instructionBudget );
        return std::shared_ptr<GameplayLuaRuntime>(
            runtime, []( GameplayLuaRuntime *owned ) noexcept {
                // The destructor owns luaL_unref/lua_close. Never begin
                // object/member destruction on a foreign thread.
                if( std::this_thread::get_id() != owned->mState->ownerThread )
                    std::terminate();
                delete owned;
            } );
    }

    GameplayLuaRuntime::~GameplayLuaRuntime() noexcept
    {
        // Defense in depth for any future ownership-path regression. create()
        // is currently the only legal construction path because both the
        // constructor and destructor are private.
        if( mState && std::this_thread::get_id() != mState->ownerThread )
            std::terminate();
    }

    void GameplayLuaRuntime::loadScript( const std::string &scriptId, const std::string &source )
    {
        State &state = *mState;
        requireOwnerThread( state );
        if( scriptId.empty() )
            throw GameplayLuaError( "gameplay lua: scriptId must not be empty" );
        // Hot reload is NOT part of Round 3: a duplicate id is rejected
        // BEFORE any state is registered (no stale registry-ref leak).
        if( state.envRefs.contains( scriptId ) )
            throw GameplayLuaError( "gameplay lua: script '" + scriptId +
                                    "' is already loaded (hot reload is not part of Round 3)" );
        lua_State *L = state.L;
        StackGuard guard( L, lua_gettop( L ) );

        // 1. Fresh isolated _ENV for this script: its own table whose
        //    metatable __index is the shared controlled global environment.
        //    _G inside the script refers to ITS OWN env - the real global
        //    table is never reachable.
        lua_createtable( L, 0, 10 ); // [env]
        const int envIdx = lua_gettop( L );
        lua_pushvalue( L, envIdx ); // [env, env]
        lua_setfield( L, envIdx, "_G" );
        lua_rawgeti( L, REGISTRYINDEX, state.globalEnvRef ); // [env, globalEnv]
        lua_createtable( L, 0, 2 );                          // [env, globalEnv, mt]
        lua_pushvalue( L, -2 );                              // [env, globalEnv, mt, globalEnv]
        lua_setfield( L, -2, "__index" );                    // mt.__index = globalEnv
        lua_pushstring( L, "script environment" );
        lua_setfield( L, -2, "__metatable" ); // protected from get/setmetatable
        lua_setmetatable( L, envIdx );        // [env, globalEnv]
        lua_pop( L, 1 );                      // [env]

        // 2. Per-script namespace isolation: every script receives its own
        //    SHALLOW COPIES of the sandbox namespace tables. The contained C
        //    functions / closures are shared values, but the table OBJECTS
        //    themselves are never mutable-shared between scripts - script A
        //    sabotaging bus.send/math.abs/table.insert can therefore never
        //    poison script B or a later C.
        for( const char *ns : { "bus", "world", "math", "string", "table", "utf8" } )
        {
            lua_rawgeti( L, REGISTRYINDEX, state.globalEnvRef ); // [env, globalEnv]
            lua_getfield( L, -1, ns );                           // [env, globalEnv, master]
            if( lua_type( L, -1 ) != TTABLE )
            {
                lua_pop( L, 2 );
                throw GameplayLuaError( std::string( "gameplay lua: sandbox namespace '" ) + ns +
                                        "' is missing" );
            }
            const int masterIdx = lua_gettop( L );
            lua_createtable( L, 0, 8 );            // [env, globalEnv, master, copy]
            copyTableShallow( L, masterIdx, -1 );
            lua_setfield( L, envIdx, ns );         // [env, globalEnv, master] (copy -> env)
            lua_pop( L, 2 );                       // [env]
        }

        // 3. Compile the chunk and bind it to this _ENV.
        if( luaL_loadbufferx( L, source.data(), source.size(), scriptId.c_str(), "t" ) != OK )
            throw GameplayLuaError( "script '" + scriptId + "': load: " + luaErrorText( L ) );
        lua_pushvalue( L, envIdx ); // [env, chunk, env]
        if( !lua_setupvalue( L, -2, 1 ) )
        {
            lua_pop( L, 2 );
            throw GameplayLuaError( "script '" + scriptId + "': chunk has no _ENV upvalue" );
        } // [env, chunk] (env copy consumed as the upvalue)

        // 4. Execute the top-level code (budgeted; bus.* is NOT available
        //    there - no cause exists).
        InvocationContext topLevel; // cause == nullptr -> bindings reject
        topLevel.remainingInstructions = mInstructionBudget;
        state.invocationStack.push_back( topLevel );
        struct ContextPopper
        {
            State &s;
            ~ContextPopper() { s.invocationStack.pop_back(); }
        } popper{ state };
        ThreadStateScope scope( &state );
        if( pcall( L, 0, 0 ) != OK )
        {
            if( topLevel.budgetExceeded || isBudgetSentinel( L, -1 ) )
                throw GameplayLuaBudgetError( "script '" + scriptId +
                                              "': run: instruction budget exceeded" );
            throw GameplayLuaError( "script '" + scriptId + "': run: " + luaErrorText( L ) );
        }

        // 5. Store the env reference (owns the script's function table).
        lua_pushvalue( L, envIdx );
        const int ref = luaL_ref( L, REGISTRYINDEX );
        state.envRefs.emplace( scriptId, ref );
    }

    bool GameplayLuaRuntime::hasScript( const std::string &scriptId ) const
    {
        requireOwnerThread( *mState );
        return mState->envRefs.contains( scriptId );
    }

    void GameplayLuaRuntime::setMaxInvocationDepth( std::size_t depth )
    {
        requireOwnerThread( *mState );
        mMaxInvocationDepth = depth;
    }

    std::size_t GameplayLuaRuntime::maxInvocationDepth() const
    {
        requireOwnerThread( *mState );
        return mMaxInvocationDepth;
    }

    void GameplayLuaRuntime::setInstructionBudget( std::uint64_t budget )
    {
        requireOwnerThread( *mState );
        mInstructionBudget = budget;
    }

    std::uint64_t GameplayLuaRuntime::instructionBudget() const
    {
        requireOwnerThread( *mState );
        return mInstructionBudget;
    }

    bool GameplayLuaRuntime::hasFunction( const std::string &scriptId,
                                          const std::string &functionName ) const
    {
        State &state = *mState;
        requireOwnerThread( state );
        lua_State *L = state.L;
        StackGuard guard( L, lua_gettop( L ) );
        const auto it = state.envRefs.find( scriptId );
        if( it == state.envRefs.end() )
            return false;
        lua_rawgeti( L, REGISTRYINDEX, it->second ); // env
        lua_getfield( L, -1, functionName.c_str() ); // env, fn
        const bool isFunction = lua_type( L, -1 ) == TFUNCTION;
        return isFunction;
    }

    void GameplayLuaRuntime::invoke( const std::string &scriptId,
                                     const std::string &functionName,
                                     const std::string &principal,
                                     const CommunicationEnvelope &envelope,
                                     std::vector<CommunicationEnvelope> &replies )
    {
        State &state = *mState;
        requireOwnerThread( state );
        if( state.invocationStack.size() >= mMaxInvocationDepth )
            throw GameplayLuaError(
                "gameplay lua: invocation depth limit exceeded (" +
                std::to_string( mMaxInvocationDepth ) +
                ") - the query recursion must terminate before reaching the C++ stack limit" );
        lua_State *L = state.L;
        StackGuard guard( L, lua_gettop( L ) );

        const auto it = state.envRefs.find( scriptId );
        if( it == state.envRefs.end() )
            throw GameplayLuaError( "gameplay lua: no script '" + scriptId + "' is loaded" );
        lua_rawgeti( L, REGISTRYINDEX, it->second ); // env
        lua_getfield( L, -1, functionName.c_str() ); // env, fn
        if( lua_type( L, -1 ) != TFUNCTION )
            throw GameplayLuaError( "gameplay lua: script '" + scriptId + "' has no function '" +
                                    functionName + "'" );
        lua_remove( L, -2 ); // fn

        pushEnvelopeSnapshot( L, envelope ); // fn, snapshot

        InvocationContext ctx;
        ctx.cause = &envelope;
        ctx.replies = &replies;
        ctx.principal = principal;
        ctx.remainingInstructions = mInstructionBudget;
        state.invocationStack.push_back( ctx );
        struct ContextPopper
        {
            State &s;
            ~ContextPopper() { s.invocationStack.pop_back(); }
        } popper{ state };
        ThreadStateScope scope( &state );
        // NOTE: the budget hook is installed ONCE at construction; it is NOT
        // re-armed here - a nested query can therefore never reset the
        // budget countdown (Round-3 hardening).
        if( pcall( L, 1, 0 ) != OK )
        {
            if( isBudgetSentinel( L, -1 ) )
                throw GameplayLuaBudgetError(
                    "script '" + scriptId + "' function '" + functionName +
                    "': instruction budget exceeded" );
            throw GameplayLuaError( "script '" + scriptId + "' function '" + functionName +
                                    "': " + luaErrorText( L ) );
        }
    }

    Handler GameplayLuaRuntime::bridgeHandler(
        std::weak_ptr<GameplayLuaRuntime> runtime, ScriptBinding binding )
    {
        // The registered ActionRegistry handler captures the runtime only
        // through a weak_ptr - never a raw `this` - so a destroyed runtime
        // turns this route into a safe no-op instead of leaving a dangling
        // pointer behind (provable lifetime, tested).
        return [weak = std::move( runtime ), binding = std::move( binding )](
                   const CommunicationEnvelope &envelope,
                   std::vector<CommunicationEnvelope> &replies ) {
            std::shared_ptr<GameplayLuaRuntime> runtime = weak.lock();
            if( !runtime )
                return; // runtime destroyed: stale route, deliberate no-op
            runtime->invoke( binding.scriptId, binding.functionName, binding.principal,
                             envelope, replies );
        };
    }
} // namespace world::scripting
