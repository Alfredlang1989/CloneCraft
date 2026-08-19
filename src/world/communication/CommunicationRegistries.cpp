#include "world/communication/CommunicationRegistries.h"

#include "world/communication/CommunicationError.h"

namespace world::communication
{
    namespace
    {
        std::string kindName( EnvelopeKind kind )
        {
            switch( kind )
            {
                case EnvelopeKind::Command: return "Command";
                case EnvelopeKind::Query: return "Query";
                case EnvelopeKind::Event: return "Event";
                case EnvelopeKind::Reply: return "Reply";
            }
            return std::to_string( static_cast<int>( kind ) );
        }
    } // namespace

    bool isValidPayloadSchema( PayloadSchema schema )
    {
        switch( schema )
        {
            case PayloadSchema::Any:
            case PayloadSchema::None:
            case PayloadSchema::BlockPlace:
            case PayloadSchema::BlockTarget:
            case PayloadSchema::PropertySet:
            case PayloadSchema::CommandResult:
            case PayloadSchema::Query:
            case PayloadSchema::EventValue:
                return true;
        }
        return false;
    }

    void requirePayloadSchema( PayloadSchema schema, bool allowAny, const char *what )
    {
        if( !isValidPayloadSchema( schema ) )
            throw CommunicationError( std::string( what ) + ": unknown PayloadSchema value " +
                                     std::to_string( static_cast<int>( schema ) ) );
        if( schema == PayloadSchema::Any && !allowAny )
            throw CommunicationError( std::string( what ) +
                                     ": 'Any' is not a valid concrete payload schema" );
    }

    bool isValidEnvelopeKind( EnvelopeKind kind )
    {
        switch( kind )
        {
            case EnvelopeKind::Command:
            case EnvelopeKind::Query:
            case EnvelopeKind::Event:
            case EnvelopeKind::Reply:
                return true;
        }
        return false;
    }

    bool isConcreteSchema( PayloadSchema schema )
    {
        return schema != PayloadSchema::Any;
    }

    const char *schemaName( PayloadSchema schema )
    {
        switch( schema )
        {
            case PayloadSchema::Any: return "Any";
            case PayloadSchema::None: return "None";
            case PayloadSchema::BlockPlace: return "BlockPlace";
            case PayloadSchema::BlockTarget: return "BlockTarget";
            case PayloadSchema::PropertySet: return "PropertySet";
            case PayloadSchema::CommandResult: return "CommandResult";
            case PayloadSchema::Query: return "Query";
            case PayloadSchema::EventValue: return "EventValue";
        }
        return "Unknown";
    }

    bool schemaAccepts( PayloadSchema signalSchema, PayloadSchema slotSchema )
    {
        if( slotSchema == PayloadSchema::Any )
            return true;
        return slotSchema == signalSchema;
    }

    bool payloadMatches( const Payload &payload, PayloadSchema schema )
    {
        switch( schema )
        {
            case PayloadSchema::Any:
                return true;
            case PayloadSchema::None:
                return std::holds_alternative<std::monostate>( payload );
            case PayloadSchema::BlockPlace:
                return std::holds_alternative<BlockPlacePayload>( payload );
            case PayloadSchema::BlockTarget:
                return std::holds_alternative<BlockTargetPayload>( payload );
            case PayloadSchema::PropertySet:
                return std::holds_alternative<PropertySetPayload>( payload );
            case PayloadSchema::CommandResult:
                return std::holds_alternative<CommandResultPayload>( payload );
            case PayloadSchema::Query:
                return std::holds_alternative<QueryPayload>( payload );
            case PayloadSchema::EventValue:
                return std::holds_alternative<EventValuePayload>( payload );
        }
        return false;
    }

    void SignalRegistry::checkCanDeclare( const std::string &action, EnvelopeKind kind,
                                          PayloadSchema schema ) const
    {
        requirePayloadSchema( schema, /*allowAny=*/false, "signal" );
        if( !isValidEnvelopeKind( kind ) )
            throw CommunicationError( "cannot declare signal for invalid EnvelopeKind" );
        if( !world::isNamespacedId( action ) )
            throw CommunicationError( "signal action '" + action +
                                     "' must be namespaced as <namespace>:<name>" );
        const Key key{ action, kind };
        const auto it = mSignals.find( key );
        if( it != mSignals.end() && it->second.schema != schema )
            throw CommunicationError( "signal '" + action + "' kind '" + kindName( kind ) +
                                     "' is already declared with payload schema '" +
                                     schemaName( it->second.schema ) + "'" );
    }

    void SignalRegistry::declare( const std::string &action, EnvelopeKind kind,
                                  PayloadSchema schema )
    {
        checkCanDeclare( action, kind, schema );
        mSignals.try_emplace( Key{ action, kind }, Definition{ schema } );
    }

    bool SignalRegistry::contains( const std::string &action, EnvelopeKind kind ) const
    {
        return mSignals.find( Key{ action, kind } ) != mSignals.end();
    }

    PayloadSchema SignalRegistry::schemaOf( const std::string &action,
                                            EnvelopeKind kind ) const
    {
        const auto it = mSignals.find( Key{ action, kind } );
        if( it == mSignals.end() )
            throw CommunicationError( "no signal declared for action '" + action +
                                     "' kind '" + kindName( kind ) + "'" );
        return it->second.schema;
    }

    void SignalRegistry::erase( const std::string &action, EnvelopeKind kind )
    {
        mSignals.erase( Key{ action, kind } );
    }

    void SlotRegistry::insert( const Definition &definition )
    {
        const Key key{ definition.action, definition.kind, definition.receiver,
                       definition.context, definition.capability };
        if( mSlots.find( key ) != mSlots.end() )
            throw CommunicationError( "a slot for action '" + definition.action +
                                     "' of this kind/receiver/context/capability is "
                                     "already registered" );
        mSlots.emplace( key, definition );
    }

    bool SlotRegistry::contains( const std::string &action, EnvelopeKind kind,
                                 const std::string &receiver, const std::string &context,
                                 const std::string &capability ) const
    {
        return mSlots.find( Key{ action, kind, receiver, context, capability } ) !=
               mSlots.end();
    }

    const SlotRegistry::Definition *SlotRegistry::find(
        const std::string &action, EnvelopeKind kind, const std::string &receiver,
        const std::string &context, const std::string &capability ) const
    {
        // Same deterministic priority as the router (M02/M03): exact
        // receiver+context -> receiver-only -> context-only -> wildcard, with
        // the exact capability preferred over the capability wildcard inside
        // every tier.
        const auto lookup = [&]( const std::string &recv, const std::string &ctx,
                                 const std::string &cap ) {
            const Key key{ action, kind, recv, ctx, cap };
            const auto it = mSlots.find( key );
            return it == mSlots.end() ? nullptr : &it->second;
        };
        if( const Definition *slot = lookup( receiver, context, capability ) ) return slot;
        if( const Definition *slot = lookup( receiver, context, "" ) ) return slot;
        if( const Definition *slot = lookup( receiver, "", capability ) ) return slot;
        if( const Definition *slot = lookup( receiver, "", "" ) ) return slot;
        if( const Definition *slot = lookup( "", context, capability ) ) return slot;
        if( const Definition *slot = lookup( "", context, "" ) ) return slot;
        if( const Definition *slot = lookup( "", "", capability ) ) return slot;
        return lookup( "", "", "" );
    }

    void SlotRegistry::erase( const std::string &action, EnvelopeKind kind,
                              const std::string &receiver, const std::string &context,
                              const std::string &capability )
    {
        mSlots.erase( Key{ action, kind, receiver, context, capability } );
    }

    void ActionRegistry::insert( const std::string &actionId, Handler handler )
    {
        if( actionId.empty() )
            throw CommunicationError( "refusing to register an action with an empty id" );
        if( !handler )
            throw CommunicationError( "refusing to register an empty action '" + actionId + "'" );
        if( mActions.find( actionId ) != mActions.end() )
            throw CommunicationError( "an action '" + actionId + "' is already registered" );
        mActions.emplace( actionId, std::move( handler ) );
    }

    const Handler *ActionRegistry::find( const std::string &actionId ) const
    {
        const auto it = mActions.find( actionId );
        return it == mActions.end() ? nullptr : &it->second;
    }

    void ActionRegistry::erase( const std::string &actionId )
    {
        mActions.erase( actionId );
    }
} // namespace world::communication
