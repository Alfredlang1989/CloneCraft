local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end
local function land_factor()
    local s=noise2(0.0000065,801)*0.78+noise2(0.0000210,802)*0.17+noise2(0.0000750,803)*0.05
    return smoothstep(-0.035,0.105,s)
end
local function rain()
    return clamp(0.5+0.5*(noise2(0.000028,611)*0.76+noise2(0.000105,613)*0.24),0,1)
end
local function temp()
    return clamp(0.5+0.5*(noise2(0.000024,612)*0.78+noise2(0.000095,614)*0.22),0,1)
end
local function geology()
    local continental=0.5+0.5*(noise2(0.000040,111)*0.72+noise2(0.000145,113)*0.28)
    local rugged=0.5+0.5*(noise2(0.000055,112)*0.68+noise2(0.000190,114)*0.32)
    return clamp(continental*0.58+rugged*0.42,0,1),continental,rugged
end
function sample(seed)
    local macro,continental=geology()
    local rolls=1.0-math.abs(noise2(0.000145,631))
    local inland=smoothstep(0.36,0.64,continental)
    local below_mountains=1.0-smoothstep(0.54,0.68,macro)
    return clamp(inland*below_mountains*(0.35+rolls*0.65)*land_factor(),0,1)
end
