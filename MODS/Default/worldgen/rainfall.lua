function sample(seed)
    -- Keep wet and dry climate bands large enough to form real biome regions.
    return 0.5 + 0.5 *
        (noise2(0.000028, 611) * 0.76 + noise2(0.000105, 613) * 0.24)
end
