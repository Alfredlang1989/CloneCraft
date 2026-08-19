#include "world/scripting/GameplayContentRuntime.h"

#include "world/communication/BlockCommandHandlers.h"
#include "world/communication/CommunicationEnvelope.h"
#include "world/communication/CommunicationRegistries.h"
#include "world/registry/Registry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace world::scripting
{
    using communication::BlockPlacePayload;
    using communication::BlockTargetPayload;
    using communication::CommandResultPayload;
    using communication::CommunicationEnvelope;
    using communication::EnvelopeKind;
    using communication::PayloadSchema;

    struct GameplayContentRuntime::State
    {
        struct Placement
        {
            std::string id;
            BlockAddress address;
            std::uint16_t runtimeId = 0;
            bool replaceOccupied = false;
        };

        struct Invocation
        {
            std::string script;
            std::string function;
            std::string principal;
            std::string sender;
            std::string receiver;
            std::string context;
            std::string action;
            EnvelopeKind kind = EnvelopeKind::Command;
            std::string targetPlacement;
            std::optional<std::string> payloadTargetPlacement;
        };

        struct Bootstrap
        {
            std::vector<Placement> placements;
            Invocation invocation;
            bool complete = false;
        };

        State( communication::CommunicationRuntime &runtime, WorldState &worldState,
               std::shared_ptr<GameplayLuaRuntime> luaRuntime ) :
            bus( runtime ), world( worldState ), lua( std::move( luaRuntime ) )
        {
        }

        communication::CommunicationRuntime &bus;
        WorldState &world;
        std::shared_ptr<GameplayLuaRuntime> lua;
        std::vector<Bootstrap> bootstraps;
        std::map<std::string, BlockAddress> placementAddresses;
    };

    namespace
    {
        using json = nlohmann::json;

        std::string readTextFile( const std::filesystem::path &path )
        {
            std::ifstream file( path, std::ios::binary );
            if( !file )
                throw GameplayContentError( "could not open gameplay content file '" +
                                            path.string() + "'" );
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        json readJsonFile( const std::filesystem::path &path )
        {
            try
            {
                return json::parse( readTextFile( path ) );
            }
            catch( const json::parse_error &error )
            {
                throw GameplayContentError( path.string() + ": JSON parse error at byte " +
                                            std::to_string( error.byte ) + ": " + error.what() );
            }
        }

        void requireObject( const json &value, const std::string &where )
        {
            if( !value.is_object() )
                throw GameplayContentError( where + " must be an object" );
        }

        void requireArray( const json &value, const std::string &where )
        {
            if( !value.is_array() )
                throw GameplayContentError( where + " must be an array" );
        }

        void checkFields( const json &object, const std::set<std::string> &allowed,
                          const std::string &where )
        {
            requireObject( object, where );
            for( auto it = object.begin(); it != object.end(); ++it )
                if( !allowed.contains( it.key() ) )
                    throw GameplayContentError( where + ": unknown field '" + it.key() + "'" );
        }

        std::string requireString( const json &object, const char *field,
                                   const std::string &where )
        {
            if( !object.contains( field ) || !object[field].is_string() )
                throw GameplayContentError( where + ": '" + field +
                                            "' must be a non-empty string" );
            const std::string result = object[field].get<std::string>();
            if( result.empty() )
                throw GameplayContentError( where + ": '" + field + "' must not be empty" );
            return result;
        }

        std::int64_t requireInt64( const json &object, const char *field,
                                   const std::string &where )
        {
            if( !object.contains( field ) )
                throw GameplayContentError( where + ": missing integer field '" + field + "'" );
            const json &value = object[field];
            if( value.is_number_unsigned() )
            {
                const std::uint64_t raw = value.get<std::uint64_t>();
                if( raw > static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max() ) )
                    throw GameplayContentError( where + ": '" + field + "' exceeds int64" );
                return static_cast<std::int64_t>( raw );
            }
            if( !value.is_number_integer() )
                throw GameplayContentError( where + ": '" + field + "' must be an integer" );
            return value.get<std::int64_t>();
        }

        std::int64_t checkedAdd( std::int64_t left, std::int64_t right,
                                 const std::string &where )
        {
            if( ( right > 0 && left > std::numeric_limits<std::int64_t>::max() - right ) ||
                ( right < 0 && left < std::numeric_limits<std::int64_t>::min() - right ) )
                throw GameplayContentError( where + ": surface height + offset overflows int64" );
            return left + right;
        }

        EnvelopeKind parseKind( const std::string &text, const std::string &where )
        {
            if( text == "command" ) return EnvelopeKind::Command;
            if( text == "event" ) return EnvelopeKind::Event;
            if( text == "query" ) return EnvelopeKind::Query;
            throw GameplayContentError( where + ": kind must be 'command', 'event' or 'query'" );
        }

        PayloadSchema parseSchema( const std::string &text, const std::string &where )
        {
            if( text == "none" ) return PayloadSchema::None;
            if( text == "block_place" ) return PayloadSchema::BlockPlace;
            if( text == "block_target" ) return PayloadSchema::BlockTarget;
            if( text == "property_set" ) return PayloadSchema::PropertySet;
            if( text == "query" ) return PayloadSchema::Query;
            if( text == "event_value" ) return PayloadSchema::EventValue;
            throw GameplayContentError( where + ": unknown payload schema '" + text + "'" );
        }

        std::filesystem::path safeContentPath( const std::filesystem::path &root,
                                               const std::string &relative,
                                               const std::string &where )
        {
            const std::filesystem::path path( relative );
            if( path.empty() || path.is_absolute() )
                throw GameplayContentError( where + ": file must be relative to the content root" );
            for( const auto &part : path )
                if( part == ".." )
                    throw GameplayContentError( where + ": file must not escape the content root" );
            return root / path;
        }

        const GameplayContentRuntime::State::Placement *findPlacement(
            const GameplayContentRuntime::State::Bootstrap &bootstrap,
            const std::string &id,
            const std::string &where )
        {
            for( const auto &placement : bootstrap.placements )
                if( placement.id == id )
                    return &placement;
            throw GameplayContentError( where + ": unknown placement '" + id + "'" );
        }
    } // namespace

    GameplayContentRuntime::GameplayContentRuntime( std::unique_ptr<State> state ) :
        mState( std::move( state ) )
    {
    }

    GameplayContentRuntime::~GameplayContentRuntime() = default;

    std::shared_ptr<GameplayContentRuntime> GameplayContentRuntime::loadIfPresent(
        const std::filesystem::path &contentRoot,
        communication::CommunicationRuntime &bus,
        communication::DelayedMessageScheduler &scheduler,
        WorldState &world,
        const BlockIdTable &idTable,
        SurfaceHeightResolver surfaceHeight )
    {
        const std::filesystem::path manifestPath = contentRoot / "gameplay.json";
        if( !std::filesystem::exists( manifestPath ) )
            return {};
        if( !surfaceHeight )
            throw GameplayContentError( "gameplay content requires a surface-height resolver" );

        const json root = readJsonFile( manifestPath );
        checkFields( root, { "scripts", "handlers", "bootstraps" }, manifestPath.string() );
        for( const char *array : { "scripts", "handlers", "bootstraps" } )
            if( !root.contains( array ) )
                throw GameplayContentError( manifestPath.string() + ": missing '" + array + "'" );
            else
                requireArray( root[array], manifestPath.string() + ": '" + array + "'" );

        std::shared_ptr<GameplayLuaRuntime> lua =
            GameplayLuaRuntime::create( bus, scheduler, world );
        auto state = std::make_unique<State>( bus, world, lua );

        std::set<std::string> scriptIds;
        std::size_t index = 0;
        for( const json &entry : root["scripts"] )
        {
            const std::string where = manifestPath.string() + ": scripts[" +
                                      std::to_string( index++ ) + "]";
            checkFields( entry, { "id", "file" }, where );
            const std::string id = requireString( entry, "id", where );
            if( !scriptIds.insert( id ).second )
                throw GameplayContentError( where + ": duplicate script id '" + id + "'" );
            const std::filesystem::path file = safeContentPath(
                contentRoot, requireString( entry, "file", where ), where );
            lua->loadScript( id, readTextFile( file ) );
        }

        index = 0;
        for( const json &entry : root["handlers"] )
        {
            const std::string where = manifestPath.string() + ": handlers[" +
                                      std::to_string( index++ ) + "]";
            checkFields( entry, { "action", "kind", "receiver", "context", "capability",
                                  "payloadSchema", "script", "function", "principal" },
                         where );
            const std::string script = requireString( entry, "script", where );
            if( !scriptIds.contains( script ) )
                throw GameplayContentError( where + ": unknown script '" + script + "'" );
            std::optional<std::string> capability;
            if( entry.contains( "capability" ) )
                capability = requireString( entry, "capability", where );
            bus.registerHandler(
                requireString( entry, "action", where ),
                parseKind( requireString( entry, "kind", where ), where ),
                GameplayLuaRuntime::bridgeHandler(
                    lua, { script, requireString( entry, "function", where ),
                           requireString( entry, "principal", where ) } ),
                requireString( entry, "receiver", where ),
                requireString( entry, "context", where ), capability,
                parseSchema( requireString( entry, "payloadSchema", where ), where ) );
        }

        index = 0;
        for( const json &entry : root["bootstraps"] )
        {
            const std::string where = manifestPath.string() + ": bootstraps[" +
                                      std::to_string( index++ ) + "]";
            checkFields( entry, { "placements", "invoke" }, where );
            if( !entry.contains( "placements" ) )
                throw GameplayContentError( where + ": missing 'placements'" );
            requireArray( entry["placements"], where + ": 'placements'" );
            if( !entry.contains( "invoke" ) )
                throw GameplayContentError( where + ": missing 'invoke'" );

            State::Bootstrap bootstrap;
            std::set<std::string> localPlacementIds;
            std::size_t placementIndex = 0;
            for( const json &placement : entry["placements"] )
            {
                const std::string placementWhere = where + ": placements[" +
                                                   std::to_string( placementIndex++ ) + "]";
                checkFields( placement, { "id", "block", "x", "z", "surfaceOffset",
                                          "replaceOccupied" },
                             placementWhere );
                State::Placement parsed;
                parsed.id = requireString( placement, "id", placementWhere );
                if( !localPlacementIds.insert( parsed.id ).second ||
                    state->placementAddresses.contains( parsed.id ) )
                    throw GameplayContentError( placementWhere + ": duplicate placement id '" +
                                                parsed.id + "'" );
                const std::string block = requireString( placement, "block", placementWhere );
                try
                {
                    parsed.runtimeId = idTable.indexOf( block );
                }
                catch( const std::exception &error )
                {
                    throw GameplayContentError( placementWhere + ": " + error.what() );
                }
                if( parsed.runtimeId == 0u )
                    throw GameplayContentError( placementWhere + ": AIR cannot be bootstrapped" );
                const std::int64_t x = requireInt64( placement, "x", placementWhere );
                const std::int64_t z = requireInt64( placement, "z", placementWhere );
                const std::int64_t offset =
                    requireInt64( placement, "surfaceOffset", placementWhere );
                const BlockAddress column = fromOriginOffset( x, 0, z );
                const std::int64_t y = checkedAdd( surfaceHeight( column ), offset,
                                                   placementWhere );
                parsed.address = withOriginRelativeY( column, y );
                if( placement.contains( "replaceOccupied" ) )
                {
                    if( !placement["replaceOccupied"].is_boolean() )
                        throw GameplayContentError(
                            placementWhere + ": 'replaceOccupied' must be a boolean" );
                    parsed.replaceOccupied = placement["replaceOccupied"].get<bool>();
                }
                state->placementAddresses.emplace( parsed.id, parsed.address );
                bootstrap.placements.push_back( std::move( parsed ) );
            }
            if( bootstrap.placements.empty() )
                throw GameplayContentError( where + ": at least one placement is required" );

            const json &invoke = entry["invoke"];
            checkFields( invoke, { "script", "function", "principal", "sender", "receiver",
                                   "context", "action", "kind", "target", "payloadTarget" },
                         where + ": invoke" );
            bootstrap.invocation.script = requireString( invoke, "script", where + ": invoke" );
            if( !scriptIds.contains( bootstrap.invocation.script ) )
                throw GameplayContentError( where + ": invoke references unknown script '" +
                                            bootstrap.invocation.script + "'" );
            bootstrap.invocation.function = requireString( invoke, "function", where + ": invoke" );
            bootstrap.invocation.principal = requireString( invoke, "principal", where + ": invoke" );
            bootstrap.invocation.sender = requireString( invoke, "sender", where + ": invoke" );
            bootstrap.invocation.receiver = requireString( invoke, "receiver", where + ": invoke" );
            bootstrap.invocation.context = requireString( invoke, "context", where + ": invoke" );
            bootstrap.invocation.action = requireString( invoke, "action", where + ": invoke" );
            bootstrap.invocation.kind = parseKind(
                requireString( invoke, "kind", where + ": invoke" ), where + ": invoke" );
            bootstrap.invocation.targetPlacement =
                requireString( invoke, "target", where + ": invoke" );
            (void)findPlacement( bootstrap, bootstrap.invocation.targetPlacement,
                                 where + ": invoke.target" );
            if( invoke.contains( "payloadTarget" ) )
            {
                bootstrap.invocation.payloadTargetPlacement =
                    requireString( invoke, "payloadTarget", where + ": invoke" );
                (void)findPlacement( bootstrap, *bootstrap.invocation.payloadTargetPlacement,
                                     where + ": invoke.payloadTarget" );
            }
            state->bootstraps.push_back( std::move( bootstrap ) );
        }

        return std::shared_ptr<GameplayContentRuntime>(
            new GameplayContentRuntime( std::move( state ) ) );
    }

    std::size_t GameplayContentRuntime::updateBootstraps()
    {
        std::size_t completed = 0;
        for( State::Bootstrap &bootstrap : mState->bootstraps )
        {
            if( bootstrap.complete )
                continue;

            bool ready = true;
            for( const State::Placement &placement : bootstrap.placements )
            {
                const std::optional<std::uint16_t> existing =
                    mState->world.blockAt( placement.address );
                if( !existing.has_value() )
                {
                    ready = false;
                    break;
                }
                if( *existing != 0u && *existing != placement.runtimeId &&
                    !placement.replaceOccupied )
                    throw GameplayContentError(
                        "gameplay bootstrap target is occupied and replacement was not declared" );
            }
            if( !ready )
                continue;

            for( const State::Placement &placement : bootstrap.placements )
            {
                if( mState->world.blockAt( placement.address ) == placement.runtimeId )
                    continue; // idempotent retry after an interrupted bootstrap
                if( mState->world.blockAt( placement.address ).value_or( 0u ) != 0u )
                {
                    // Replacement is an explicit remove + place transaction through the
                    // same authoritative bus commands as player interaction. It never
                    // bypasses WorldState or materializes a chunk ahead of worldgen.
                    CommunicationEnvelope remove;
                    remove.messageId = mState->bus.nextMessageId();
                    remove.kind = EnvelopeKind::Command;
                    remove.sender = bootstrap.invocation.sender;
                    remove.receiver = "world:state";
                    remove.context = "core:world";
                    remove.action = communication::ACTION_BLOCK_REMOVE;
                    remove.target = WorldStateTarget( placement.address );
                    const auto removeResult = mState->bus.dispatch( remove );
                    const auto *removeReply = removeResult.replies.size() == 1u
                                                  ? std::get_if<CommandResultPayload>(
                                                        &removeResult.replies.front().payload )
                                                  : nullptr;
                    if( !removeReply || !removeReply->ok )
                        throw GameplayContentError(
                            "gameplay bootstrap removal rejected" +
                            std::string( removeReply && !removeReply->error.empty()
                                             ? ": " + removeReply->error
                                             : "" ) );
                }
                CommunicationEnvelope place;
                place.messageId = mState->bus.nextMessageId();
                place.kind = EnvelopeKind::Command;
                place.sender = bootstrap.invocation.sender;
                place.receiver = "world:state";
                place.context = "core:world";
                place.action = communication::ACTION_BLOCK_PLACE;
                place.target = WorldStateTarget( placement.address );
                place.payload = BlockPlacePayload{ placement.runtimeId };
                const auto result = mState->bus.dispatch( place );
                if( result.replies.size() != 1u )
                    throw GameplayContentError( "gameplay bootstrap placement produced no reply" );
                const auto *reply =
                    std::get_if<CommandResultPayload>( &result.replies.front().payload );
                if( !reply || !reply->ok )
                    throw GameplayContentError(
                        "gameplay bootstrap placement rejected" +
                        std::string( reply && !reply->error.empty() ? ": " + reply->error : "" ) );
            }

            const State::Placement *target = findPlacement(
                bootstrap, bootstrap.invocation.targetPlacement, "bootstrap invocation" );
            CommunicationEnvelope cause;
            cause.messageId = mState->bus.nextMessageId();
            cause.kind = bootstrap.invocation.kind;
            cause.sender = bootstrap.invocation.sender;
            cause.receiver = bootstrap.invocation.receiver;
            cause.context = bootstrap.invocation.context;
            cause.action = bootstrap.invocation.action;
            cause.target = WorldStateTarget( target->address );
            if( bootstrap.invocation.payloadTargetPlacement )
            {
                const State::Placement *payloadTarget = findPlacement(
                    bootstrap, *bootstrap.invocation.payloadTargetPlacement,
                    "bootstrap payload target" );
                cause.payload = BlockTargetPayload{ payloadTarget->address };
            }

            std::vector<CommunicationEnvelope> replies;
            mState->lua->invoke( bootstrap.invocation.script,
                                 bootstrap.invocation.function,
                                 bootstrap.invocation.principal,
                                 cause, replies );
            bootstrap.complete = true;
            ++completed;
        }
        return completed;
    }

    std::size_t GameplayContentRuntime::pendingBootstrapCount() const
    {
        std::size_t count = 0;
        for( const State::Bootstrap &bootstrap : mState->bootstraps )
            if( !bootstrap.complete )
                ++count;
        return count;
    }

    std::optional<BlockAddress> GameplayContentRuntime::placementAddress(
        const std::string &id ) const
    {
        const auto it = mState->placementAddresses.find( id );
        return it == mState->placementAddresses.end()
                   ? std::nullopt
                   : std::optional<BlockAddress>{ it->second };
    }

    std::shared_ptr<GameplayLuaRuntime> GameplayContentRuntime::luaRuntime() const
    {
        return mState->lua;
    }
} // namespace world::scripting
