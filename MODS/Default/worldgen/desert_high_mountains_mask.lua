function sample(seed)
    local crown = 0.5 + 0.5 * noise2(0.000080, 116)
    local patch = 0.5 + 0.5 * noise2(0.000090, 641)
    return 0.62 + crown * patch * 0.38
end
