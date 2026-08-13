function sample(seed)
    local crown = 0.5 + 0.5 * noise2(0.00073, 116)
    return 0.65 + crown * 0.35
end
