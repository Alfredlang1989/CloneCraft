-- M03 Round 4: Block A of the mandatory visible two-block bus proof.
--
-- A self-schedules a delayed message to itself every 5 real seconds. Each
-- callback alternates A's packed visible tint (red/green) and increments a
-- block-scoped callback-count Sidecar, both through the normal bus
-- (Command -> WorldState). After the 6th callback A sends an Event to Block B
-- through the SAME bus; B's own Lua handler changes B's visible color.
--
-- There is NO direct A->B C++ call and NO Lua callback stored in the
-- scheduler: the scheduler transports only plain CommunicationEnvelopes.
-- Lua never writes raw Sidecars directly - every mutation goes through
-- bus.send(Command) to the authoritative WorldState.

-- Working memory (per-script _ENV, persists across invocations). The
-- authoritative record is the block-scoped Sidecar written via WorldState.
local count = 0
local self_id = nil
local peer = nil -- Block B's typed address payload, delivered at start()

-- Packed generic render tints use 0xRRGGBBAA. Their meaning remains content.
local RED_TINT = 0xFF4040FF
local GREEN_TINT = 0x40FF40FF
local function nextColor( n )
    if n % 2 == 1 then return RED_TINT end
    return GREEN_TINT
end

-- Starts the proof: records A's own runtime id, learns Block B's address from
-- the typed block_target payload, and schedules the first
-- self-timer. msg.target carries Block A's address.
function start( msg )
    local t = { x = msg.target.x, y = msg.target.y, z = msg.target.z }
    assert( msg.reply_to == nil, "peer data must not misuse reply_to" )
    local here = world.get_block( t )
    if here.loaded then
        self_id = here.block_id
    end
    assert( msg.payload.schema == "block_target", "start requires a block target" )
    peer = { x = msg.payload.target.x,
             y = msg.payload.target.y,
             z = msg.payload.target.z }
    bus.schedule_after_ms( 5000,
        { kind = "event", receiver = "block:a", context = "test:bus",
          action = "test:timer.elapsed", target = t,
          payload = { schema = "none" } } )
end

-- Each 5-second callback: alternate A's color, bump the callback count, and
-- reschedule. After the 6th callback, send the Event to Block B instead.
function on_timer( msg )
    -- If Block A was removed or replaced before this callback, drop it
    -- safely (no crash, no reschedule, no sidecar write).
    local t = { x = msg.target.x, y = msg.target.y, z = msg.target.z }
    local here = world.get_block( t )
    if not here.loaded or here.block_id ~= self_id then
        return
    end

    count = count + 1
    local color = nextColor( count )

    -- Mutate A's Sidecars through the bus (Command -> WorldState). Lua never
    -- writes raw Sidecars directly.
    bus.send( { kind = "command", receiver = "world:state", context = "core:world",
                action = "core:property.set", target = t,
                payload = { schema = "property_set", property = "test:visual_tint",
                            value = { value_type = "u32", value = color } } } )
    bus.send( { kind = "command", receiver = "world:state", context = "core:world",
                action = "core:property.set", target = t,
                payload = { schema = "property_set", property = "test:callback_count",
                            value = { value_type = "u32", value = count } } } )

    if count >= 6 then
        -- After the 6th callback, send the Event to Block B through the same
        -- bus. target = Block B (the peer).
        bus.send( { kind = "event", receiver = "block:b", context = "test:bus",
                    action = "test:peer.change_color", target = peer,
                    payload = { schema = "event_value",
                                value = { value_type = "u32", value = GREEN_TINT } } } )
        return -- do not reschedule
    end

    -- Reschedule self every 5 seconds.
    bus.schedule_after_ms( 5000,
        { kind = "event", receiver = "block:a", context = "test:bus",
          action = "test:timer.elapsed", target = t,
          payload = { schema = "none" } } )
end
