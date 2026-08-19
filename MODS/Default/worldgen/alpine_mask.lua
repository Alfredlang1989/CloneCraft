function sample(seed)
    local crown = 0.5 + 0.5 * noise2(0.000080, 115)
    return 0.85 + crown * 0.15
end
