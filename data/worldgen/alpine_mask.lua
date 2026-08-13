local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local macro=continental*0.58+rugged*0.42
    local mountain=smoothstep(0.54,0.68,macro)
    local massif=smoothstep(0.70,0.82,macro)
    local hot=smoothstep(0.52,0.76,temp)
    local dry=1.0-smoothstep(0.30,0.52,rain)
    local arid=math.sqrt(hot*dry)
    return clamp(mountain*(1.0-0.68*massif)*(1.0-0.45*arid),0,1)
end
