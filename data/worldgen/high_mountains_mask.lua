function sample(seed)
    local continental = 0.5 + 0.5 * noise2(0.00034, 111)
    local rugged = 0.5 + 0.5 * noise2(0.00047, 112)
    local macro = continental*0.58+rugged*0.42
    local crown = 0.5 + 0.5 * noise2(0.00073, 116)
    local massif = math.max(0.0, math.min(1.0,(macro-0.62)/0.17))
    return massif*(0.55+crown*0.45)
end
