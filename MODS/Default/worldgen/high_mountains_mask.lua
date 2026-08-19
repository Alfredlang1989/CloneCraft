function sample(seed)
    local continental = 0.5 + 0.5 *
        (noise2(0.000040, 111) * 0.72 + noise2(0.000145, 113) * 0.28)
    local rugged = 0.5 + 0.5 *
        (noise2(0.000055, 112) * 0.68 + noise2(0.000190, 114) * 0.32)
    local macro = continental*0.58+rugged*0.42
    local crown = 0.5 + 0.5 * noise2(0.000080, 116)
    local massif = math.max(0.0, math.min(1.0,(macro-0.62)/0.17))
    return massif*(0.55+crown*0.45)
end
