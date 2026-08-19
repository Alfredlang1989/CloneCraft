function sample(seed)
    -- Climate changes over whole regions; the second octave only softens edges.
    return 0.5 + 0.5 *
        (noise2(0.000024, 612) * 0.78 + noise2(0.000095, 614) * 0.22)
end
