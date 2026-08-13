local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local macro=continental*0.58+rugged*0.42
    local moist=smoothstep(0.45,0.70,rain)
    local thermal=1.0-smoothstep(0.22,0.43,math.abs(temp-0.52))
    local lowland=1.0-smoothstep(0.60,0.76,macro)
    return clamp(math.sqrt(moist*thermal)*lowland*0.78,0,1)
end
