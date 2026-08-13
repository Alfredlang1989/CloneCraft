function sample(seed)
    local continental = 0.5 + 0.5 * noise2(0.00034, 111)
    local rugged = 0.5 + 0.5 * noise2(0.00047, 112)
    return continental * 0.58 + rugged * 0.42
end
