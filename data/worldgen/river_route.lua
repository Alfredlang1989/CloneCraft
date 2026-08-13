local TUNNEL_START_STRENGTH=0.35
local TUNNEL_FULL_STRENGTH=0.85

local function clamp(v,lo,hi)
    if v<lo then return lo end
    if v>hi then return hi end
    return v
end

local function smoothstep(a,b,x)
    local t=clamp((x-a)/(b-a),0,1)
    return t*t*(3-2*t)
end

-- This deliberately follows the geology-only signal exposed as massif_mask.lua.
-- It must not use the climate-filtered high-mountain biome masks: desert high
-- mountains are still real massifs for river routing. Do not reconstruct
-- surface_height here: the real surface is biome-adjusted in C++, and duplicating
-- the old height formula was what made river carving dig far below Y=0 after the
-- v18.3.5 terrain change.
local function land_factor()
    local s=noise2(0.0000065,801)*0.78+noise2(0.0000210,802)*0.17+noise2(0.0000750,803)*0.05
    return smoothstep(-0.035,0.105,s)
end

local function mountain_strength()
    if land_factor()<0.52 then return 0.0 end
    local continental=0.5+0.5*(noise2(0.000040,111)*0.72+noise2(0.000145,113)*0.28)
    local rugged=0.5+0.5*(noise2(0.000055,112)*0.68+noise2(0.000190,114)*0.32)
    local macro=continental*0.58+rugged*0.42
    local crown=0.5+0.5*noise2(0.000080,116)
    local massif=smoothstep(0.62,0.79,macro)
    return clamp(massif*(0.55+crown*0.45),0,1)
end

local function river_distance()
    if land_factor()<0.52 then return 1.0 end
    local wx=noise2(0.0007, 701)*120.0
    local wz=noise2(0.0007, 702)*120.0
    return math.abs(noise2(0.0016, 703,wx,wz))
end

local function tunnel_factor()
    return smoothstep(TUNNEL_START_STRENGTH,TUNNEL_FULL_STRENGTH,mountain_strength())
end

local function clearance()
    return 10.0+tunnel_factor()*40.0
end

-- Outside a real massif this is the normal open-river valley mask. The actual
-- U/V cross-section lives in river_profile.lua and is carved from the real
-- biome-adjusted surface. In a massif this path is disabled and the tunnel path
-- below takes over.
function valley_mask(seed)
    if tunnel_factor()>0.0 then return 1.0 end
    return river_distance()
end

-- In a massif, carve only the chamber ABOVE the fixed Y=0 river. The generic
-- surface-layer geometry below yields Y=1..ceiling; there is intentionally no
-- tunnel floor-space pass and therefore no river-generated Air below Y=0.
function tunnel_mask(seed)
    local factor=tunnel_factor()
    if factor<=0.0 then return 1.0 end
    return river_distance()/(1.0+factor*1.35)
end

function tunnel_ceiling_center(seed) return clearance() end
function tunnel_ceiling_inner(seed) return math.max(8.0,clearance()*0.82) end
function tunnel_ceiling_middle(seed) return math.max(6.0,clearance()*0.60) end
function tunnel_ceiling_outer(seed) return math.max(4.0,clearance()*0.36) end
