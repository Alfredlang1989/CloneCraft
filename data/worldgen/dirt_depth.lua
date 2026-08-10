function sample(seed)
    local broad = noise2(0.018, 201)
    local fine = noise2(0.061, 202)
    return math.max(1.0, math.floor(3.0 + broad*1.5 + fine*0.65 + 0.5))
end
