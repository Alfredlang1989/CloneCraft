-- Lowland macro relief only. Biome-specific offsets, detail and ridges are
-- applied in C++ from MODS/Default/biomes.json after all mask fields are sampled.
-- Keeping this field near Y=0 prevents ordinary plains from inheriting the
-- old global +38 block pedestal.
function sample(seed)
    local broad = noise2(0.0048, 101)
    local detail = noise2(0.022, 102)
    local rolling = 1.0 - math.abs(noise2(0.0024, 103))
    rolling = rolling * rolling
    return 8.0 + broad*6.0 + detail*1.5 + rolling*3.0
end
