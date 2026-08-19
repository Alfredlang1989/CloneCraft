-- M03 Round 4: Block B of the mandatory visible two-block bus proof.
--
-- B's own data-driven Lua handler reacts to the Event A sends after its 6th
-- callback (action "test:peer.change_color"). B changes its visible color to
-- green through the normal bus (Command -> WorldState). There is no
-- direct A->B C++ call: A reaches B only through the bus Event.

function on_peer_change( msg )
    -- msg.target carries Block B's address (the Event's target = Block B).
    -- B changes its own visible color via the bus -> WorldState.
    local t = { x = msg.target.x, y = msg.target.y, z = msg.target.z }
    bus.send( { kind = "command", receiver = "world:state", context = "core:world",
                action = "core:property.set", target = t,
                payload = { schema = "property_set", property = "test:visual_tint",
                            value = { value_type = msg.payload.value.value_type,
                                      value = msg.payload.value.value } } } )
end
