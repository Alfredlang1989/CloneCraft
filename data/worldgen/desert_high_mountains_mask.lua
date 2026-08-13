function sample(seed)
    local crown = 0.5 + 0.5 * noise2(0.00073, 116)
    local patch = 0.5 + 0.5 * noise2(0.00082, 641)
    return 0.62 + crown * patch * 0.38
end
