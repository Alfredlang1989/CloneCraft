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
    local continental = 0.5 + 0.5 * noise2(0.00034, 111)
    local rugged = 0.5 + 0.5 * noise2(0.00047, 112)
    local macro = continental*0.58 + rugged*0.42
    local rolls = 1.0 - math.abs(noise2(0.00135, 631))
    local inland = smoothstep(0.36, 0.64, continental)
    local below_mountains = 1.0 - smoothstep(0.54, 0.68, macro)
    return clamp(inland * below_mountains * (0.35 + rolls*0.65), 0.0, 1.0)
end
