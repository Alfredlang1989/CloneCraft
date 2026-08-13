local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066,611)
    local temp=0.5+0.5*noise2(0.00058,612)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local macro=continental*0.58+rugged*0.42
    local patch=0.5+0.5*noise2(0.00082,604)
    local hot=smoothstep(0.52,0.76,temp)
    local dry=1.0-smoothstep(0.30,0.52,rain)
    local arid=math.sqrt(hot*dry)
    local nonmountain=1.0-smoothstep(0.60,0.76,macro)
    return clamp(arid*nonmountain*(0.82+patch*0.28)*1.15,0,1)
end
