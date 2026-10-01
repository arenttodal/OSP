# DSP research method

## Bake-off philosophy

> Prefer the simplest algorithm that wins the listening test.

The advantage of this project comes from thousands of small listening decisions, so
the research harness matters as much as the plugin. Every candidate algorithm is
rendered through the same fixtures, on the same sources, with the same seed, next to:

- **A — basic sampler**: `--engine A` (bandlimited resampling, velocity → gain).
- **B — simple randomised sampler**: `--engine B` (A + independent per-note gain,
  detune and start-offset randomisation). Deliberately naive.
- **C — the current advanced engine** (added per experiment).

A component advances only if it is more playable, more beautiful, keeps source
identity, beats the simpler method often enough to justify its complexity, fails
gracefully and fits real-time constraints. Otherwise it is removed.

Numbers (metrics, ground-truth comparisons) prevent implausible behaviour; they never
replace listening.

## How to run a comparison

```sh
R=./build/apps/research-renderer/research-renderer
# Same source, same fixture, two engines
$R --source src.wav --fixture repetition --engine A --output out/rep-A.wav --metrics out/rep-A.json
$R --source src.wav --fixture repetition --engine B --output out/rep-B.wav --metrics out/rep-B.json
# Whole corpus, both baselines
$R --corpus research/corpus --profile standard --engines A,B
```

Render files are named `<fixture>.wav` (engine A) and `<fixture>.<engine>.wav`
(other engines) under `research/renders/<source-folder>/`. For blind tests, copy the
files to a folder with shuffled names and keep the key separately.

## Planned experiments (do not start before Phase 0 review)

1. **Pitch engine bake-off** — sustained synth, vocal, bowed, organ, pluck at root
   −24, −12, 0, +12, +24 with A (bandlimited resampling), B (Signalsmith Stretch), C
   (another formant-aware strategy). Judge identity, beauty, artifacts, transient,
   stereo and useful range blind. See the Phase 0 report for the recommended setup.
2. **Continuation** — Trampeorgel, vocal vowel, nyckelharpa, tremolo source, synth:
   single loop vs optimised loop/crossfade vs multi-loop vs multi-loop + movement,
   60 s renders (`--profile sustain`). Decide whether spectral continuation is needed
   for MVP.
3. **Performance synthesis** — real repeated-performance sets: one take as source,
   generate repeats with identical retrigger vs independent randomisation (baseline B)
   vs a correlated performance model. Compare against the hidden real takes.
4. **Dynamics** — one expressive source at velocities 20–127: gain only vs gain +
   filter vs a dynamic performance model. The difference should be obvious blind.

## Analysis descriptors available today

See `docs/analysis-schema.md`. Highlights useful for the next phases:

- `pitch.trackHz` / `pitchConfidence` series, `stabilityCents`, `vibrato`.
- `envelope.endsWhileSounding` (no natural release → release grafting needed),
  `tremolo`, `secondaryPeakCount`, `sustainFluctuationDb`, onsets.
- `spectral.harmonicEnergyRatio`, `periodicity`, `meanFlatness` (tonal vs noisy),
  centroid/flux/flatness series.
- `stereo.width`, band widths, correlation (to detect stereo damage after processing).
