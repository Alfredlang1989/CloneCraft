function sample(seed)
    local continental = 0.5 + 0.5 * noise2(0.00034, 111)
    local rugged = 0.5 + 0.5 * noise2(0.00047, 112)
    local macro = continental*0.58+rugged*0.42
    return math.max(0.0, math.min(1.0,(macro-0.55)/0.24))
end
