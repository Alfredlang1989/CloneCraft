function sample(seed)
    local patch = 0.5 + 0.5 * noise2(0.000105, 604)
    return 0.82 + patch * 0.18
end
