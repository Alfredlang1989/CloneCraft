function sample(seed)
    local wx = noise3(0.014, 0.011, 0.014, 401) * 5.0
    local wz = noise3(0.014, 0.011, 0.014, 402) * 5.0
    return noise3(0.045, 0.03, 0.045, 403, wx, 0.0, wz)
end
