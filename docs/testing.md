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

## Benchmarks

```sh
./build/apps/research-renderer/research-renderer --benchmark                       # 24 voices, 48 kHz, 128-sample blocks, dense retriggers
./build/apps/research-renderer/research-renderer --benchmark --voices 16 --no-retrigger
```

Each block is timed individually; the report gives mean, p99 and worst callback time
as a percentage of the block budget (2.67 ms at 48 kHz/128). Target (spec §64): mean
below ~25 % for 16 sustained voices on Apple Silicon, with margin on the worst case.
Worst-case numbers on shared CI/cloud machines include scheduler noise.
