function sample(seed)
    local warp = noise3(0.01, 0.008, 0.01, 301) * 8.0
    return noise3(0.03, 0.024, 0.03, 302, warp, -warp*0.35, -warp)
end
