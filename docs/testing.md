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

The layer EQ: `[unit][eq]` (drawn response = measured response at 44.1-96 kHz, null when off,
extremes and automation finite, click-free switching, per-layer isolation and modulation in the
engine), `[plugin][eq]` (defaults, IDs, isolation by solo, recall, older sessions, automation at
four rates), `[ui][eq-ui]` (the editor in the waveform, activation by drag, typing, one at a time,
ARP + MOD, modulation drops; screenshots eq-01..09) and the hidden `[eq-cpu]` measurement. A
render config may carry `"eq": { "enabled": true, "bands": { "hp": { "frequencyHz": 90, "steep":
true }, "bell": { "frequencyHz": 3200, "gainDb": -4, "q": 2 } } }` (layer A).

Modulation: `[unit][mod]` (shapes, phase drift and block-size independence, host sync, ONE
SHOT, envelope stages, route rules, no-route null, per-voice independence), `[plugin][mod]`
(parameters, routes, recall, older sessions, curves and undo; every source with the ARP,
Granular and three layers), `[ui][mod-ui]` (the tabbed panel, the window keeping its size, the RATE
menu, tab drags, polarity, halos (geometry, hover readout, depth through zero, several sources),
the routes and curve popovers, scales and recall; screenshots modui-01..15 with
`OSP_SNAPSHOT_DIR`), `[ui][andor-ui]` (right-click assignment, the mod wheel as a source,
Granular hover-to-open, START and envelope-time routes, ENV on DRIVE, halo clearance, the source
card, ADVANCED, the hidden MIX triangle; screenshots andor-01..12), `[unit][mod][regression]`
(the global LFO after ONE SHOT, without voices, across transport changes and repeated keys) and the hidden `[mod-cpu]`
measurement (16 routes against none).

The arpeggiator: `[unit][arp]` (core: patterns, sample-exact host-grid timing over 60/90/120/174 BPM
x 44.1/48/96 kHz x 32..512 blocks x all rates, free running, loops/jumps/tempo changes, gate,
pedal, panic, hand-overs, a stuck-note stress test, RANDOM determinism, the research `"arp"`
config) and `[plugin][arp]` (parameters and recall, playback, the transport, every REIMAGINED
mode/routing and effect, REVERSE/LOOP/Granular/layers, lifecycle, the stage's CPU). Hidden:
`[arp-baseline]` (ARP off renders sessions from the build before it bit-identically:
`OSP_ARP_BASELINE=write` with the old build, `=compare` with the new, same
`OSP_ARP_BASELINE_DIR`) and `[arp-ui]` (the keyboard control, the inline editor and the
window resizing; screenshots with `OSP_SNAPSHOT_DIR`, under xvfb-run). A render config's `"modulation"` block may set `"modWheel": 0..1` (the MOD WHEEL source for the
whole render) and route `"source": "mod wheel"`. A render config may
carry `"arp": { "enabled": true, "pattern": "up|down|updown|played|random|chord|downup|up&down|down&up|converge|diverge|con&diverge|pinkyup|pinkyupdown|thumbup|thumbupdown|randomother|randomonce", "rate":
"1/16", "gate": 0.75, "octaves": 2, "swing": 0.15, "bpm": 110, "transport": true }`: the fixture's notes are
arpeggiated as the plugin does it, for engines A, B and C alike (`transport` false: no host,
free running).

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
| `transients-1.json` | 6 (§19) | attacks at ±12/±24: transposed vs transient preservation |
| `dynamics-2.json` | 5 (§34) | velocity 20→127: attack gain vs transient mixing (velocity on the pick alone) |

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

### Shaping settings in experiments

An `instrument` block (in a config or an experiment condition) may carry a `shaping`
block with the plugin's popup settings; anything left out keeps its default, and
`"preset": "neutral"` starts from a transparent setting (filter off, no velocity colour):

```json
"instrument": {
  "attackSeconds": 0.005, "releaseSeconds": 0.7,
  "shaping": {
    "life": { "mode": "natural|loose|fray", "pitchCents": 4, "tone": 0.3, "attack": 0.25 },
    "dynamics": { "curve": "soft|linear|hard", "tone": 0.35 },
    "character": { "type": "lp24|lp12|hp12|bp12|tilt|off", "minHz": 450, "maxHz": 18000,
                   "resonance": 0.1, "drive": 0.12, "envAmount": 0.1,
                   "envAttackSeconds": 0.005, "envDecaySeconds": 0.7 },
    "movement": { "mode": "drift|tape|chorus|pulse|shaper",
                  "drift": { "speed": 0.7, "pitch": 0.25, "tone": 0.4 },
                  "tape": { "wow": 0.5, "flutter": 0.35, "wear": 0.35 },
                  "chorus": { "rate": 0.45, "width": 0.6, "stereo": 0.6 },
                  "pulse": { "rate": 0.55, "shape": 0.3, "stereo": 0.3 },
                  "shaper": { "pattern": 3, "rate": "1/4|1/8|1/8T|1/16|1/16T|1/32",
                              "target": "vol|filter|both", "smooth": 0.3 } },
    "space": { "type": "room|hall|plate|spring", "decaySeconds": 1.8, "preDelayMs": 8, "size": 0.5,
               "damping": 0.4, "modulation": 0.4, "width": 1.0, "lowCutHz": 100, "highCutHz": 12000 },
    "echo": { "type": "tape|bbd", "sync": true, "division": 5, "timeMs": 375, "feedback": 0.45,
              "tone": 0.5, "age": 0.35, "stereo": "mono|pingpong|wide" },
    "drive": { "mode": "tube|tape|crunch", "tone": 0.5, "body": 0.5 }
  }
}
```

`"chamber"` (SPACE v1) is read as `"hall"`, which took its place. The ECHO macro is
`"macros": { "echo": 0.0 }` (0 = off); DRIVE's amount is `"macros": { "drive": 0.0 }` (0 = the
stage is bypassed, bit-exact). `division` indexes 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D,
1/4, 1/2T, 1/4D, 1/2, 1/2D, 1/1.

Older files may still give `"movement": { "a", "b", "c" }`: those are the selected mode's
three settings. Research renders have no host transport, so SHAPER uses its stopped-transport
clock (120 BPM, the pattern starts with the first note).
