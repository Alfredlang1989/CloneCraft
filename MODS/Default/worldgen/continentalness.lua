function sample(seed)
    return 0.5 + 0.5 *
        (noise2(0.000040, 111) * 0.72 + noise2(0.000145, 113) * 0.28)
end
