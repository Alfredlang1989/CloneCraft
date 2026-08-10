function sample(seed)
    local rain=0.5+0.5*noise2(0.00066, 611)
    local temp=0.5+0.5*noise2(0.00058, 612)
    local continental=0.5+0.5*noise2(0.00034, 111)
    local rugged=0.5+0.5*noise2(0.00047, 112)
    local macro=continental*0.58+rugged*0.42
    local patch=0.5+0.5*noise2(0.00105, 604)
    local hot=math.max(0.0,(temp-0.56)/0.38)
    local dry=math.max(0.0,(0.43-rain)/0.36)
    local nonmountain=1.0-math.max(0.0,math.min(1.0,(macro-0.58)/0.18))
    return hot*dry*nonmountain*(0.75+patch*0.25)
end
