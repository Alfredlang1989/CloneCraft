local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local crown=0.5+0.5*noise2(0.00073,116)
    local patch=0.5+0.5*noise2(0.00082,641)
    local macro=continental*0.58+rugged*0.42
    local massif=smoothstep(0.66,0.80,macro)
    local hot=smoothstep(0.52,0.76,temp)
    local dry=1.0-smoothstep(0.30,0.52,rain)
    local arid=math.sqrt(hot*dry)
    return clamp(massif*arid*(0.78+crown*0.25)*(0.85+patch*0.25)*1.70,0,1)
end
