function sample(seed)
    local patch = 0.5 + 0.5 * noise2(0.000090, 651)
    return 0.78 + patch * 0.22
end
