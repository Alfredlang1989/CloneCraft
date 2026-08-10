local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
function sample(seed)
    local rain=0.5+0.5*noise2(0.00066, 611)
    local temp=0.5+0.5*noise2(0.00058, 612)
    local continental=0.5+0.5*noise2(0.00034, 111)
    local rugged=0.5+0.5*noise2(0.00047, 112)
    local crown=0.5+0.5*noise2(0.00073, 116)
    local patch=0.5+0.5*noise2(0.00102, 641)
    local mountain=smoothstep(0.60,0.80,continental*0.58+rugged*0.42)*(0.68+crown*0.32)
    local hot=clamp((temp-0.53)/0.36,0,1)
    local dry=clamp((0.47-rain)/0.36,0,1)
    return mountain*hot*dry*(0.78+patch*0.22)
end
