local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end
local function smoothstep(a, b, x)
    local t = clamp((x-a)/(b-a), 0.0, 1.0)
    return t*t*(3.0-2.0*t)
end
function sample(seed)
    local band = 0.5 + 0.5 * noise2(0.000095, 621)
    local detail = 0.5 + 0.5 * noise2(0.000220, 622)
    return 0.30 + smoothstep(0.42, 0.70, band) * (0.50 + detail * 0.20)
end
