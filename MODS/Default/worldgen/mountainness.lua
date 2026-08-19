function sample(seed)
    local continental = 0.5 + 0.5 *
        (noise2(0.000040, 111) * 0.72 + noise2(0.000145, 113) * 0.28)
    local rugged = 0.5 + 0.5 *
        (noise2(0.000055, 112) * 0.68 + noise2(0.000190, 114) * 0.32)
    return continental * 0.58 + rugged * 0.42
end
