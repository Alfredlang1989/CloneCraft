local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local macro=continental*0.58+rugged*0.42
    local patch=0.5+0.5*noise2(0.00090,651)
    local warm=smoothstep(0.50,0.72,temp)
    local wetEnough=smoothstep(0.27,0.40,rain)
    local notWet=1.0-smoothstep(0.50,0.64,rain)
    local seasonal=wetEnough*notWet
    local lowland=1.0-smoothstep(0.60,0.76,macro)
    return clamp(math.sqrt(warm*seasonal)*lowland*(0.72+patch*0.20),0,1)
end
