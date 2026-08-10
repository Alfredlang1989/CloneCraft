-- Procedural oak. Wood and leaves are separate functions but reconstruct the
-- same branch graph from the immutable anchor seed, so both passes can run in
-- parallel without sharing mutable state.
local function rnd(seed, lane)
    local x = (seed ~ (lane * 1103515245 + 12345)) & 0x7fffffffffffffff
    x = (x ~ (x << 13)) & 0x7fffffffffffffff
    x = (x ~ (x >> 7)) & 0x7fffffffffffffff
    x = (x ~ (x << 17)) & 0x7fffffffffffffff
    return (x & 0xffff) / 65535.0
end

local function round(v)
    if v >= 0 then return math.floor(v + 0.5) end
    return math.ceil(v - 0.5)
end

local dirs = {
    { 1.0, 0.0 }, { 0.707, 0.707 }, { 0.0, 1.0 }, { -0.707, 0.707 },
    { -1.0, 0.0 }, { -0.707, -0.707 }, { 0.0, -1.0 }, { 0.707, -0.707 }
}

local function params(seed)
    local height = 8 + math.floor(rnd(seed, 1) * 5.0)
    local branches = 4 + math.floor(rnd(seed, 2) * 3.0)
    return height, branches
end

local function branch(seed, i, height)
    local start = math.floor(height * (0.47 + rnd(seed, 10 + i) * 0.28))
    local len = 3 + math.floor(rnd(seed, 20 + i) * 3.0)
    local dirIndex = 1 + ((i * 3 + math.floor(rnd(seed, 30) * 8.0)) % 8)
    local d = dirs[dirIndex]
    local rise = 1 + math.floor(rnd(seed, 40 + i) * 3.0)
    return start, d[1] * len, rise, d[2] * len
end

local function line_hit(dx, dy, dz, ex, ey, ez)
    local steps = math.max(math.abs(ex), math.abs(ey), math.abs(ez))
    steps = math.max(1, math.ceil(steps * 1.25))
    for s = 0, steps do
        local t = s / steps
        if dx == round(ex * t) and dy == round(ey * t) and dz == round(ez * t) then
            return true
        end
    end
    return false
end

function wood(dx, dy, dz, seed)
    local height, branches = params(seed)
    if dx == 0 and dz == 0 and dy >= 0 and dy < height then return 1 end

    for i = 1, branches do
        local start, ex, rise, ez = branch(seed, i, height)
        if dy >= start and line_hit(dx, dy - start, dz, ex, rise, ez) then return 1 end
    end
    return 0
end

local function leaf_blob(dx, dy, dz, cx, cy, cz, seed, lane, rx, ry, rz)
    local nx = (dx - cx) / rx
    local ny = (dy - cy) / ry
    local nz = (dz - cz) / rz
    local d = nx * nx + ny * ny + nz * nz
    local edge = 0.82 + rnd(seed ~ (dx * 1973 + dy * 9277 + dz * 26699), lane) * 0.35
    return d <= edge
end

function leaves(dx, dy, dz, seed)
    local height, branches = params(seed)
    if dy < 3 then return 0 end

    if leaf_blob(dx, dy, dz, 0, height - 1, 0, seed, 100, 3.6, 2.8, 3.6) then
        return 1
    end
    if leaf_blob(dx, dy, dz, 0, height + 1, 0, seed, 101, 2.7, 2.0, 2.7) then
        return 1
    end

    for i = 1, branches do
        local start, ex, rise, ez = branch(seed, i, height)
        local cx = round(ex)
        local cy = start + round(rise)
        local cz = round(ez)
        if leaf_blob(dx, dy, dz, cx, cy, cz, seed, 120 + i, 2.5, 2.0, 2.5) then
            return 1
        end
    end
    return 0
end
