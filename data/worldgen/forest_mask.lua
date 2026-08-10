function sample(seed)
    local rain = 0.5 + 0.5 * noise2(0.00066, 611)
    local temp = 0.5 + 0.5 * noise2(0.00058, 612)
    local continental = 0.5 + 0.5 * noise2(0.00034, 111)
    local rugged = 0.5 + 0.5 * noise2(0.00047, 112)
    local mountain = continental*0.58+rugged*0.42
    local moist = math.max(0.0,(rain-0.48)/0.34)
    local temperate = 1.0-math.min(1.0,math.abs(temp-0.52)/0.38)
    local lowland = 1.0-math.max(0.0,(mountain-0.57)/0.20)
    return moist*temperate*lowland
end
