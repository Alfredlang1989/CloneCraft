-- Slender birch with a high, airy crown. Wood/leaves deliberately use the same
-- deterministic seed math so their passes remain fully parallel.
local function rnd(seed, lane)
    local x = (seed ~ (lane * 1664525 + 1013904223)) & 0x7fffffffffffffff
    x = (x ~ (x << 11)) & 0x7fffffffffffffff
    x = (x ~ (x >> 9)) & 0x7fffffffffffffff
    x = (x ~ (x << 7)) & 0x7fffffffffffffff
    return (x & 0xffff) / 65535.0
end

local function round(v)
    if v >= 0 then return math.floor(v + 0.5) end
    return math.ceil(v - 0.5)
end

local dirs = { {1,0}, {0.707,0.707}, {0,1}, {-0.707,0.707}, {-1,0}, {-0.707,-0.707}, {0,-1}, {0.707,-0.707} }

local function height(seed)
    return 7 + math.floor(rnd(seed, 1) * 5.0)
end

local function twig(seed, i, h)
    local start = h - 4 + (i % 3)
    local len = 2 + math.floor(rnd(seed, 20 + i) * 2.0)
    local d = dirs[1 + ((i * 2 + math.floor(rnd(seed, 30) * 8.0)) % 8)]
    return start, d[1] * len, 1 + (i % 2), d[2] * len
end

local function line_hit(dx, dy, dz, ex, ey, ez)
    local steps = math.max(1, math.ceil(math.max(math.abs(ex), math.abs(ey), math.abs(ez)) * 1.3))
    for s=0,steps do
        local t=s/steps
        if dx==round(ex*t) and dy==round(ey*t) and dz==round(ez*t) then return true end
    end
    return false
end

function wood(dx, dy, dz, seed)
    local h = height(seed)
    if dx==0 and dz==0 and dy>=0 and dy<h then return 1 end
    for i=1,4 do
        local start, ex, ey, ez = twig(seed, i, h)
        if dy>=start and line_hit(dx,dy-start,dz,ex,ey,ez) then return 1 end
    end
    return 0
end

local function blob(dx,dy,dz,cx,cy,cz,seed,lane,rx,ry,rz)
    local nx=(dx-cx)/rx; local ny=(dy-cy)/ry; local nz=(dz-cz)/rz
    local d=nx*nx+ny*ny+nz*nz
    return d <= 0.82 + rnd(seed ~ (dx*313 + dy*991 + dz*1999), lane) * 0.32
end

function leaves(dx,dy,dz,seed)
    local h=height(seed)
    if dy < h-5 then return 0 end
    if blob(dx,dy,dz,0,h-1,0,seed,80,2.8,3.2,2.8) then return 1 end
    for i=1,4 do
        local start,ex,ey,ez=twig(seed,i,h)
        if blob(dx,dy,dz,round(ex),start+round(ey),round(ez),seed,90+i,1.8,1.6,1.8) then return 1 end
    end
    return 0
end
