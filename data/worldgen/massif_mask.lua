local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function smoothstep(a,b,x) local t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t) end

-- Geology-only massif strength. This intentionally ignores temperature and
-- rainfall: a hot desert massif is still a massif for rivers and terrain logic.
function sample(seed)
    local continental=0.5+0.5*noise2(0.00034,111)
    local rugged=0.5+0.5*noise2(0.00047,112)
    local macro=continental*0.58+rugged*0.42
    local crown=0.5+0.5*noise2(0.00073,116)
    local massif=smoothstep(0.62,0.79,macro)
    return clamp(massif*(0.55+crown*0.45),0,1)
end
