local function land_factor()
    local signal = noise2(0.0000065,801)*0.78
                 + noise2(0.0000210,802)*0.17
                 + noise2(0.0000750,803)*0.05
    local t = math.max(0.0, math.min(1.0, (signal + 0.035) / 0.140))
    return t*t*(3.0-2.0*t)
end

function sample(seed)
    -- River passes are land content; ocean water owns the deep-water columns.
    if land_factor() < 0.52 then return 1.0 end
    local wx=noise2(0.0007, 701)*120.0
    local wz=noise2(0.0007, 702)*120.0
    return math.abs(noise2(0.0016, 703,wx,wz))
end
