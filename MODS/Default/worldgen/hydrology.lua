local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function smoothstep(a, b, x)
    local t = clamp((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
end

-- Macro geography belongs here, not in generic C++. This is the current-data
-- port of the proven 4134b2b worldgen algorithm: one planetary land signal,
-- coast detail at faster octaves, and physical lake basins at three scales.
local function land_signal()
    return noise2(0.0000065, 801) * 0.78
         + noise2(0.0000210, 802) * 0.17
         + noise2(0.0000750, 803) * 0.05
end

local function land_factor()
    return smoothstep(-0.035, 0.105, land_signal())
end

local function climate_rain()
    local macro = noise2(0.000028, 611)
    local regional = noise2(0.000105, 613)
    return clamp(0.5 + 0.5 * (macro * 0.76 + regional * 0.24), 0.0, 1.0)
end

local function geology_mountainness()
    local continental = 0.5 + 0.5 *
        (noise2(0.000040, 111) * 0.72 + noise2(0.000145, 113) * 0.28)
    local rugged = 0.5 + 0.5 *
        (noise2(0.000055, 112) * 0.68 + noise2(0.000190, 114) * 0.32)
    return clamp(continental * 0.58 + rugged * 0.42, 0.0, 1.0)
end

local function lake_candidate()
    -- Closed contours become real water bodies because the pass graph carves
    -- every accepted candidate to its shared spill level before filling it.
    local huge = smoothstep(0.765, 0.900, 0.5 + 0.5 * noise2(0.000028, 871))
    local large = smoothstep(0.805, 0.925, 0.5 + 0.5 * noise2(0.000080, 872)) * 0.82
    local medium = smoothstep(0.860, 0.955, 0.5 + 0.5 * noise2(0.000240, 873)) * 0.58
    return math.max(huge, large, medium)
end

local function lake_strength()
    local inland = smoothstep(0.58, 0.88, land_factor())
    local wet = smoothstep(0.34, 0.61, climate_rain())
    local lowland = 1.0 - smoothstep(0.61, 0.75, geology_mountainness())
    -- The inland gate keeps lake contours away from the coastal transition.
    return clamp(lake_candidate() * inland * wet * lowland, 0.0, 1.0)
end

function ocean_mask(seed)
    return 1.0 - land_factor()
end

function land_mask(seed)
    return land_factor()
end

function ocean_level(seed)
    return 0.0
end

function ocean_water_bottom(seed)
    return -192.0
end

function lake_mask(seed)
    return lake_strength()
end

function lake_level(seed)
    -- One basin receives an almost shared regional water level, while nearby
    -- basins can sit above or below it instead of all becoming sea-level ponds.
    return 7.0 + noise2(0.000020, 881) * 5.0 + noise2(0.000070, 882) * 1.5
end

function lake_bottom(seed)
    local strength = lake_strength()
    local detail = 0.5 + 0.5 * noise2(0.00055, 883)
    return lake_level(seed) - (3.0 + strength * 17.0 + detail * 2.0)
end

function lake_water_bottom(seed)
    return lake_bottom(seed) + 1.0
end

function lake_shore_mask(seed)
    local strength = lake_strength()
    if strength >= 0.26 and strength < 0.50 then
        return 1.0 - math.abs(strength - 0.38) / 0.12
    end
    return 0.0
end
