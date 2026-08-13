local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
local function land_factor()
    local s=noise2(0.0000065,801)*0.78+noise2(0.0000210,802)*0.17+noise2(0.0000750,803)*0.05
    return smoothstep(-0.035,0.105,s)
end
function sample(seed)
    if land_factor() < 0.52 then return 1.0 end
    local wx=noise2(0.0007,701)*120.0
    local wz=noise2(0.0007,702)*120.0
    return math.abs(noise2(0.0016,703,wx,wz))
end
