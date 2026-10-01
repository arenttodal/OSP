# research/bakeoff

Phase 2 pitch-engine bake-off. Plans (`plan-*.json`) are committed; rendered runs
(`<name>/clips`, `key.json`, `listening.json`, scores) are generated and git-ignored.

```sh
R=./build/apps/research-renderer/research-renderer
$R --bakeoff research/bakeoff/plan-1.json                 # -> research/bakeoff/pitch-bakeoff-1/
$R --bakeoff-score research/bakeoff/pitch-bakeoff-1 --ratings ratings.json
```

Engines: **A** bandlimited resampling (baseline sampler), **B** Signalsmith Stretch plain
transposition, **C** Signalsmith Stretch with formant compensation (F0 hint from analysis).
All start at the analysed onset with the same level matching; clips in a group share
one length (the shortest natural length, so duration does not reveal the engine), are
faded, RMS-matched to −20 dBFS and named randomly. `key.json` maps names to engines and
holds guard-rail metrics (pitch error, spectral-envelope shift, stereo, render time).
`listening.json` has groups and clip ids only.
