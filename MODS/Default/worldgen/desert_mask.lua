function sample(seed)
    local patch = 0.5 + 0.5 * noise2(0.000090, 605)
    return 0.80 + patch * 0.20
end
