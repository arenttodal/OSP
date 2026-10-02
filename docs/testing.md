# Testing

```sh
ctest --test-dir build --output-on-failure      # everything
./build/tests/osp_tests "[unit]"                 # or [integration], [regression], [pitch], [sampler]...
```

| CTest name | What it covers |
|---|---|
| `unit` | pitch math, cents, PRNG determinism + frozen golden values, interpolation (DC, accuracy, anti-aliasing), ADSR, voice allocation/stealing/pedal, sampler pitch & sample-rate independence, WAV/AIFF/AIFF-C loading (16/24/32f, mono/stereo, 44.1–96 kHz), bad files, JSON round-trip & schema enforcement, SHA-256 vectors, corpus hashing/dedup, MIDI fixtures & SMF round-trip, analysis on synthetic ground truth |
| `integration` | file → analysis → fixture renders → metrics; determinism and block-size independence; output sample-rate changes; stored vs fresh analysis; unpitched sources; safety-check detection; full corpus run over a mixed folder incl. broken/unsupported files |
| `regression` | golden renders (below) |
| `performance-smoke` | `research-renderer --benchmark --seconds 3` must run |
| `plugin` (with `-DOSP_BUILD_PLUGIN=ON`) | the real `AudioProcessor`, headless: load → analysis → chromatic playback through `processBlock`, host sample-rate changes, root override, session save/recall after the original file is deleted (bit-identical audio), bad files keeping the previous instrument, unpitched sources |

The editor has a hidden snapshot test (needs a display):
`OSP_SNAPSHOT_DIR=/tmp xvfb-run ./build-plugin/apps/plugin/osp_plugin_tests "[ui]"`
writes `osp-editor-empty.png` / `osp-editor-loaded.png`.

## MIDI fixtures

Defined in `src/midi/MidiFixtures.cpp` (frozen, `fixtureVersion = 1`) and written to
`research/midi/*.mid` with reference note C4. The renderer generates them at the
source's root (rounded) unless `--fixture-reference` is given.

| Fixture | Content |
|---|---|
| `repetition` | 8 × reference, velocity 90, 0.5 s apart, 0.4 s long |
| `dynamics` | reference at velocities 20, 40, 60, 80, 100, 127 (1.5 s apart) |
| `register` | reference −24, −12, 0, +12, +24 (2 s each) |
| `melody` | 14-note phrase: steps, thirds, fourth, fifth, a repeated note, velocities 60–105 |
| `chords` | major, minor, sus4 (close), major and minor (wide), 2.5 s each |
| `long-hold` | one note, 60 s |
| `long-chord` | four-note chord (0, 3, 7, 10), 60 s |
| `repeated-sustains` | five notes of 5–10 s with 1 s pauses |

Profiles: `quick` = repetition, register · `standard` = repetition, dynamics,
register, melody, chords · `sustain` = long-hold, long-chord, repeated-sustains ·
`full` = all. The corpus runner never renders 60 s fixtures unless `sustain`/`full`
is requested.

## Render safety checks and metrics

Every render (CLI, corpus, tests) gets `RenderMetrics`:

- errors: NaN, Inf, unexpected channel count, invalid/unexpected sample rate, empty output
- warnings: clipping (|x| ≥ 1), near-silence (peak < −60 dBFS), DC offset > 0.01,
  pitch mismatch (isolated note > 50 cents from expectation)
- metrics: peak/RMS dBFS, duration, frames, DC per channel, spectral centroid mean/std,
  whole-render pitch (single-pitch renders only), per-note pitch checks
  (expected = source F0 × 2^((note − root)/12); notes outside 35 Hz–3.5 kHz are recorded
  but not judged), SHA-256 of the raw samples.

## Golden renders

`tests/regression/GoldenTests.cpp` defines 8 cases (vowel, pluck and 96 kHz saw
sources; repetition, register, chords, dynamics, melody; engines A and B; 44.1 and
48 kHz output). Each case is rendered twice (must be bit-identical) and compared with
`tests/regression/golden/<case>.json`:

| Check | Tolerance |
|---|---|
| accidental silence / NaN / Inf / clipping | none allowed |
| duration (frames) | exact |
| peak and RMS level | ±0.5 dB |
| spectral centroid | ±3 % |
| isolated-note frequency | ±5 cents of golden, < 10 cents from expected |
| non-determinism | two renders must match bit for bit |
| sample hash | informational (warns, does not fail) |

Update goldens only for an intentional DSP change and say so in the commit:

```sh
OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[regression]"
```

## Corpus tests

```sh
./build/apps/research-renderer/research-renderer --corpus research/corpus --profile standard --engines A,B
```

Writes `research/reports/summary.{json,md}`, per-source `analysis.json` and metrics,
and renders under `research/renders/`. One failing file never stops the run. Use
`--strict` to get a non-zero exit when any file fails or any render has errors.
`--generate-test-signals <dir>` creates a synthetic mini-corpus (including a broken
and an unsupported file) for trying the runner without the private corpus.

## Listening experiments (Phases 2–7)

The principal test is listening (spec §71). `research-renderer --experiment plan.json`
renders a blind experiment from a plan in `research/experiments/`:

- every source × variant is a **group**; every condition is a **take** (engine A, B or C
  with any `instrument` settings, a single file or the whole set with `useSet`);
- takes in a group are trimmed to a common length, faded and RMS-matched (−20 dBFS),
  written with random names (FLAC), and shuffled per group;
- `key.json` maps names to conditions and records guard rails: `repetitionScore`
  (strongest self-similarity of log-band spectra at 0.5–20 s lags — a plain loop scores
  0.7–0.9, multi-loop continuation 0.1–0.2), `seamSpikeDb` (clicks/seams: loudest 3 ms
  second-difference window re median), render time, level gain;
- `listening.json` has only what a listener may see.

`python3 research/listening/build.py <out> <run> [<run>...]` builds the **listening lab**
page (one tab per run; ratings, a best pick and free-text notes per group, saved to the
page's database). The lab copies are 320 kbps MP3; the lossless renders stay in the runs.

| Plan | Phase | Question |
|---|---|---|
| `research/bakeoff/plan-1.json` (`--bakeoff`) | 2 | which pitch engine (resampling / Signalsmith / formant) |
| `continuation-1.json` | 3 | can you hear the loop in 60 s holds (naive / best / multi / multi + movement) |
| `performance-1.json` | 4 | repeated notes: identical / independent random / LIFE 50 % / 80 % |
| `dynamics-1.json` | 5 | velocity 20→127: gain / gain + filter / model 50 % / 100 % |
| `continuum-1.json` | 6 | Original ↔ Reimagined at 0/25/50/75/100 % |
| `multisample-1.json` | 7 | one file vs the whole set |

Other research checks: `--continuation-report <file|dir>` (stable region, jump quality,
release per file) and `--register-test <dir>` (multi-pitch ground truth, spec §70: hide
each pitch, rebuild from the rest, compare brightness with the real recording).

## Benchmarks

```sh
./build/apps/research-renderer/research-renderer --benchmark                       # 24 voices, 48 kHz, 128-sample blocks, dense retriggers
./build/apps/research-renderer/research-renderer --benchmark --voices 16 --no-retrigger
./build/apps/research-renderer/research-renderer --benchmark --engine C --voices 16 --no-retrigger   # the OSP engine
```

Each block is timed individually; the report gives mean, p99 and worst callback time
as a percentage of the block budget (2.67 ms at 48 kHz/128). Target (spec §64): mean
below ~25 % for 16 sustained voices on Apple Silicon, with margin on the worst case.
Worst-case numbers on shared CI/cloud machines include scheduler noise.
