function sample(seed)
    local rolls = 1.0 - math.abs(noise2(0.00135, 631))
    return 0.45 + rolls * 0.55
end
