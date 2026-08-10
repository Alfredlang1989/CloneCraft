local WATER_HALF_WIDTH = 0.030
local BANK_HALF_WIDTH = 0.064
local VALLEY_HALF_WIDTH = 0.110

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function smoothstep(a, b, x)
    local t = clamp((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
end

local function river_distance()
    local wx = noise2(0.0007, 701) * 120.0
    local wz = noise2(0.0007, 702) * 120.0
    return math.abs(noise2(0.0016, 703, wx, wz))
end

local function bank_noise()
    return noise2(0.010, 731) * 0.55 + noise2(0.027, 732) * 0.20
end

-- Natural bank height. The first shoreline blocks deliberately live around
-- river level. Only farther away do they climb to +1/+2, with a little noise.
function bank_surface(seed)
    local d = river_distance()
    if d <= WATER_HALF_WIDTH then return 0.0 end

    local q = clamp((d - WATER_HALF_WIDTH) / (BANK_HALF_WIDTH - WATER_HALF_WIDTH), 0.0, 1.0)
    local rise = 2.35 * math.pow(q, 1.55)
    local n = bank_noise() * (0.25 + 0.75 * q)
    return clamp(rise + n, 0.0, 2.95)
end

-- One continuous open-valley profile replaces the old stack of flat shelves.
-- At the water edge the carve stops just above Y=0. Farther out it smoothly
-- rises toward the surrounding lowland instead of making a canal wall.
function valley_floor(seed)
    local d = river_distance()
    if d <= WATER_HALF_WIDTH then return 1.0 end

    local q = clamp((d - WATER_HALF_WIDTH) / (VALLEY_HALF_WIDTH - WATER_HALF_WIDTH), 0.0, 1.0)
    local rise = 7.0 * math.pow(smoothstep(0.0, 1.0, q), 1.20)
    local n = bank_noise() * (0.20 + 0.80 * q)
    return clamp(1.0 + rise + n, 1.0, 8.5)
end

-- U/V hybrid river bed. Deepest near the thalweg and progressively shallower
-- toward the shore. A small low-frequency wobble keeps the bottom from being a
-- perfectly extruded trough while retaining a continuous, sealed profile.
function bed_surface(seed)
    local d = river_distance()
    local r = clamp(d / WATER_HALF_WIDTH, 0.0, 1.0)
    local bowl = 1.0 - math.pow(r, 1.65)
    local depth = 1.05 + 4.15 * math.pow(bowl, 0.82)
    local wobble = noise2(0.012, 741) * 0.38 + noise2(0.031, 742) * 0.16
    return -depth + wobble
end

function water_bottom(seed)
    return bed_surface(seed) + 1.0
end

local function sediment_selector()
    local a = 0.5 + 0.5 * noise2(0.0075, 751)
    local b = 0.5 + 0.5 * noise2(0.0210, 752)
    return clamp(a * 0.72 + b * 0.28, 0.0, 1.0)
end

local function selected_mask(lo, hi, max_distance)
    local d = river_distance()
    if d > max_distance then return 1.0 end
    local s = sediment_selector()
    if s >= lo and s < hi then return d end
    return 1.0
end

-- Bed mixture. Dirt is the fallback foundation; these masks overwrite patches
-- with sand, clay and gravel. The broad noise creates banks/bed bars instead of
-- salt-and-pepper single-block noise.
function bed_sand_mask(seed) return selected_mask(0.00, 0.34, WATER_HALF_WIDTH) end
function bed_clay_mask(seed) return selected_mask(0.34, 0.61, WATER_HALF_WIDTH) end
function bed_gravel_mask(seed) return selected_mask(0.61, 0.84, WATER_HALF_WIDTH) end

-- Banks remain mostly sandy, with occasional clay patches. Unselected patches
-- keep the dirt foundation visible.
function bank_sand_mask(seed) return selected_mask(0.00, 0.76, BANK_HALF_WIDTH) end
function bank_clay_mask(seed) return selected_mask(0.76, 0.90, BANK_HALF_WIDTH) end
