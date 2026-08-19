function sample(seed)
    local continental=0.5+0.5*(noise2(0.000040,111)*0.72+noise2(0.000145,113)*0.28)
    local rugged=0.5+0.5*(noise2(0.000055,112)*0.68+noise2(0.000190,114)*0.32)
    local macro=continental*0.58+rugged*0.42
    local cold=1.0-(0.5+0.5*(noise2(0.000024,612)*0.78+noise2(0.000095,614)*0.22))
    local crown=0.5+0.5*noise2(0.000080,116)
    local high=math.max(0.0,math.min(1.0,(macro-0.68)/0.12))
    return high*(0.60+crown*0.25+cold*0.25)
end
