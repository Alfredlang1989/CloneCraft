function sample(seed)
    local wx=noise2(0.0007, 701)*120.0
    local wz=noise2(0.0007, 702)*120.0
    return math.abs(noise2(0.0016, 703,wx,wz))
end
