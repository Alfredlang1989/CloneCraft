-- Thick walkable spaghetti. Domain warp is a local block displacement; the
-- hierarchical address itself never becomes a Lua number.
function sample(seed)
    local wx = noise3(0.008, 0.006, 0.008, 501) * 14.0
    local wy = noise3(0.007, 0.007, 0.007, 502) * 8.0
    local wz = noise3(0.008, 0.006, 0.008, 503) * 14.0
    local a = noise3(0.03, 0.02, 0.03, 504, wx, wy, wz)
    local b = noise3(0.03, 0.02, 0.03, 505, -wz, -wy, wx)
    return math.max(math.abs(a), math.abs(b))
end
