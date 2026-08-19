function sample(seed)
    return 0.5 + 0.5 *
        (noise2(0.000055, 112) * 0.68 + noise2(0.000190, 114) * 0.32)
end
