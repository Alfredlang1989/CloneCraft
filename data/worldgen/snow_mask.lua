function sample(seed)
    local continental=0.5+0.5*noise2(0.00034, 111)
    local rugged=0.5+0.5*noise2(0.00047, 112)
    local macro=continental*0.58+rugged*0.42
    local cold=1.0-(0.5+0.5*noise2(0.00058, 612))
    local crown=0.5+0.5*noise2(0.00073, 116)
    local high=math.max(0.0,math.min(1.0,(macro-0.68)/0.12))
    return high*(0.60+crown*0.25+cold*0.25)
end
