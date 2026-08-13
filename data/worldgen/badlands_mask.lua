local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local band=0.5+0.5*noise2(0.00090,621)
    local warm=smoothstep(0.48,0.72,temp)
    local semidry=1.0-smoothstep(0.10,0.27,math.abs(rain-0.36))
    local inland=smoothstep(0.43,0.72,continental)
    return clamp(math.sqrt(warm*semidry)*inland*(0.75+band*0.30),0,1)
end
