# Phase 0 report — research harness + baseline engine

_Status: complete. Verified on Linux (GCC 13) and on macOS 14 / Apple Silicon in CI
(build, all tests, benchmark, `auval` AU validation: **AU VALIDATION SUCCEEDED**).
The private 80-file corpus was not available in the development environment, so
corpus figures below come from the synthetic ground-truth set. Run the corpus
locally before signing off (command at the end)._

## 1. Repository architecture

```
src/core        Prng (xoshiro256**), PitchMath, Fft, Statistics, AudioData        ┐
src/audio       pitch/SincInterpolator, envelopes/Adsr, voices/SamplerVoice,      │ osp_dsp: pure C++20,
                sampler/BaselineSampler, utility/TestSignals                      │ no JUCE, no I/O,
src/analysis    AnalysisFrames → pitch (YIN) → loudness/envelope → spectrum       │ RT-safe where it
                → onset → stereo; Analyzer orchestrates                           │ runs on audio
src/model       AnalysisData (versioned), PlaybackSource, RootChoice,             │
                ModelExchange (lock-free model hand-over)                         │
src/midi        MidiEvent, MidiFixtures (frozen, v1)                              ┘
src/io          WAV/AIFF via JUCE, SHA-256, JSON, analysis (de)serialisation, SMF   JUCE-backed
src/research    RenderSession, RenderMetrics, CorpusIndex, CorpusRunner,          research library
                Benchmark, SourceAnalysis, Fixtures, TestSignalSet                (CLI + tests share it)
apps/research-renderer   the CLI
apps/plugin              Phase 1 JUCE plugin (AU/VST3/Standalone)
tests/{unit,integration,regression,plugin}   Catch2; goldens in tests/regression/golden
research/{corpus,midi,configs,renders,reports}
```

Key decisions (all documented in `docs/architecture.md`):

- DSP and analysis are framework-free so the same code runs in tests, the CLI, a worker
  thread and the audio thread. JUCE provides file formats, MIDI files, JSON, SHA-256
  and (Phase 1) plugin hosting/GUI.
- JUCE modules are compiled once into a headless static library for tools; the plugin
  compiles its own JUCE with plugin settings.
- Rendering is block-based exactly like a host callback, with sample-accurate events:
  output is bit-identical for block sizes 32–1024 and across runs.

## 2. Completed tickets

| Ticket | Result |
|---|---|
| P0-01 Repository/CMake/JUCE/tests | CMake only, JUCE 8.0.9 + Catch2 3.9.1 via FetchContent (or local dirs), clean-tree build verified |
| P0-02 Headless renderer | `--analyze`, `--source + --midi/--fixture`, `--analysis`, `--root`, `--engine`, `--config`, `--seed`, `--sample-rate`, `--block-size`, `--metrics`; exit codes 0–4 |
| P0-03 Corpus system | recursive scan, SHA-256 ids, duplicates, unsupported files listed, `index.json` |
| P0-04 Audio loader | WAV, AIFF, AIFF-C; 8/16/24/32-bit PCM, 32-bit float; 44.1–96 kHz; mono/stereo (>2 ch: first two + warning); content sniffing; truncation instead of rejection for huge files |
| P0-05 MIDI fixtures | 8 fixtures (frozen, v1), static `.mid` files committed, generated at source root by the renderer; profiles quick/standard/sustain/full |
| P0-06 Baseline sampler | A: Kaiser-windowed sinc with stretch for anti-aliasing, ADSR, velocity→gain, 24 voices (max 64), stealing (released→quietest→oldest, 5 ms fades into tail slots), sustain pedal. B: A + independent per-note gain/detune/start randomisation (deterministic seed) |
| P0-07 Root/F0 | YIN (FFT difference function), weighted median, agreement×periodicity confidence, high/moderate/low/none, cents offset, vibrato rate/depth |
| P0-08 Envelope/onset | silences, onset, attack, peak, body slope, sustain level/fluctuation, decay estimate, ends-while-sounding, secondary peaks, tremolo, flux onsets, RMS series |
| P0-09 Spectral | centroid (+std), rolloff, flatness, flux, periodicity, harmonic energy ratio, HF/LF energy; centroid/flux/flatness series |
| P0-10 Stereo | correlation, width, side/mid, balance, low/mid/high band widths, dual-mono detection |
| P0-11 Analysis JSON | `schemaVersion` 1, every field documented in `docs/analysis-schema.md`, round-trip tested, null for unknowns |
| P0-12 Corpus runner | profiles, engines A/B, parallel jobs, failure isolation, `summary.{md,json}`, per-source analysis + metrics + renders |
| Golden renders | 8 cases; determinism, duration, level, centroid, note-frequency checks |
| Safety checks | NaN, Inf, DC, clipping, near-silence, channel count, sample rate — on every render |
| Benchmark | per-callback timing; CTest smoke test |

## 3. Test results

`ctest` (Linux, GCC 13, Release): **all suites pass** — `unit`, `integration`,
`regression`, `performance-smoke`, and `plugin` when the plugin is built
(65 core test cases + 5 plugin cases, ~19 000 assertions, ~40 s). The documented
commands were re-run from a clean copy with FetchContent: build + all tests pass.

Highlights of what is covered: frozen PRNG golden values (match the xoshiro256**
reference), sinc interpolation accuracy (< 2e-3 error in band) and anti-aliasing
(> 50 dB suppression), chromatic pitch to within 0.2 % at every combination of
source rate (44.1/48/88.2/96 kHz) and output rate (44.1/48/96 kHz), WAV/AIFF round trips
for 48 format/rate/channel combinations, malformed/empty/mislabelled files, and a full
corpus run over a folder containing broken and unsupported files.

## 4. Corpus success/failure counts

**Real corpus: not run** (files not present here). Synthetic stand-in
(`--generate-test-signals`: 13 audio files in 7 encodings at 44.1–96 kHz + 1 broken
WAV + 1 text file), `--profile standard --engines A,B`:

| | |
|---|---|
| files | 15 total: 13 analysed, 1 failed (broken header, reported and skipped), 1 unsupported (.txt) |
| pitch | 9 high, 0 moderate, 0 low, 4 none (noise, silence, impulse, 10 ms tone) |
| renders | 130: 120 ok, 10 warning (all from the silent source: `near-silent`, correct), 0 error |
| wall time | 13.8 s with 4 jobs |

## 5. Pitch-analysis observations

On synthetic ground truth every tonal source is detected at the right note with
`high` confidence: sines from C2 to C6, saws including E1 (41 Hz), Karplus-Strong pluck
(E2), a formant vowel with vibrato, vibrato (±40 c) and tremolo (12 dB) sines; cents
offsets are reported (e.g. −23 c within 1 c). Noise, silence, impulses and a 10 ms tone
never claim a pitch (`detected: false`, confidence < 0.4, warning + `null` in JSON).
Vibrato rate/depth and tremolo rate/depth are measured within ~5 %.

Expected behaviour on the real corpus, to verify:

- **Strong**: synths, organ, vowels, bowed sustains, sax — stable harmonic material.
- **Watch**: plucks with inharmonic/bright attacks (attack frames may disagree; the
  weighted median should hold), bass below 30 Hz (outside the YIN range → wrong or low
  confidence), tremolando/bowed noise (lower periodicity → may drop to `moderate`),
  frozen/processed textures (likely `low`/`none` — that is the intended honest outcome).
- Octave errors are the classic YIN failure; the agreement term in the confidence
  penalises frames that jump octaves, so a source that alternates will report `low`
  rather than a confident wrong root.

## 6. CPU benchmark

48 kHz, 128-sample blocks, 16-zero-crossing sinc, measured per callback on the
development VM (Intel Xeon @ 2.1 GHz, shared cloud machine):

| Scenario | mean / budget | p99 | worst |
|---|---|---|---|
| 16 sustained voices | 547 µs / 2667 µs = **20.5 %** | 805 µs | 3.8 ms* |
| 24 voices, dense retriggers (stealing every 50 ms) | 919 µs = **34.5 %** | 1394 µs | 6.3 ms* |

| macOS 14, Apple Silicon (GitHub-hosted VM), 24 voices dense | 697 µs = **26.1 %** | 1875 µs | 9.5 ms* |

\* worst-case spikes on shared VMs are dominated by scheduler preemption; the render
path performs no allocation, locking or I/O. Target (spec §64): < 25 % mean for 16
sustained voices — met on the Xeon VM (20.5 %); the Apple Silicon CI runner is a
virtualised, shared machine, so measure on a real Mac before drawing conclusions.
Either way the interpolator is the obvious first optimisation (see §7).

## 7. Known weaknesses

1. **Interpolator cost** grows with upward transposition (kernel stretch) and the kernel
   is recomputed per output sample. Fix later with pre-decimated (mip-mapped) sources
   and/or polyphase tables — after the pitch bake-off decides the architecture.
2. **No continuation**: voices end with the recording, so `long-hold`/`long-chord` are
   mostly silence for short sources (by design for baseline A).
3. **Baseline B** randomises gain/detune/start only (no filter). It is deliberately naive.
4. **YIN range** 30 Hz–4 kHz; analysis runs at the source rate (96 kHz costs 2x).
5. **Onsets** from spectral flux with a 3 dB/bin floor miss very soft swells.
6. **Metrics are signal-level only** (no perceptual measures yet); per-note checks are
   limited to isolated notes.
7. **Plugin keyboard merge** uses JUCE's locking `MidiKeyboardState` (debug UI only;
   to be replaced by a lock-free FIFO).
8. **Real corpus and macOS hosts untested** in this environment (see Blockers in STATUS).

## 8. Recommended architecture for the Pitch Engine Bake-Off (Phase 2)

Goal: choose the default pitch strategy by blind listening, not assumption.

**Engines**

| | Strategy | Integration |
|---|---|---|
| A | bandlimited resampling (today's baseline) | existing `SincInterpolator` voice path |
| B | Signalsmith Stretch, pitch shift with formant compensation **off** | header-only, MIT, via FetchContent into `src/audio/pitch/SignalsmithShifter` |
| C | formant-aware: Signalsmith with formant compensation **on**, formant base from our analysed F0 | same wrapper, different options |
| (C′, optional) | TD-PSOLA driven by our YIN track — cheap, classic for monophonic voice/strings | `src/audio/pitch/PsolaShifter`, only if C disappoints on vowels |

Rubber Band (GPL/commercial) and élastique stay out until licensing is decided; the
wrapper interface makes adding them a one-file change.

**Interface (offline first).** `AudioData shiftOffline (const AudioData& source,
double semitones, const PitchOptions&)` per engine. The bake-off renders each source
note *offline* at each offset — it compares sound, not real-time cost. Real-time
feasibility is measured separately with the winner (spec §27 suggests offline-rendered
register anchors at −24/−12/+12/+24 with resampling between them; the bake-off result
tells us whether anchors are needed at all).

**Material.** Choose one source per family from the corpus using analysis descriptors
(highest pitch confidence, `endsWhileSounding` for sustained families): synth
(Prophet 6), vocal vowel, bowed (nyckelharpa/violin), organ (Trampeorgel), pluck.
Offsets −24, −12, 0, +12, +24 → 5 sources × 5 offsets × 3 engines = 75 clips.

**Blinding and fairness.** New CLI command, e.g. `--bakeoff <sources.json>`, that:
renders all clips; loudness-matches them for listening (RMS to −20 dBFS, so louder ≠
better); trims to equal length; writes them under random names with a separate
`key.json`; and generates a local static HTML page per source/offset for A/B/C
preference and 1–5 ratings of identity, beauty, artifacts, transient and stereo
(results saved as JSON next to the key). No network needed.

**Supporting metrics** (guard-rails, not judges): per-clip pitch accuracy, spectral
centroid and spectral-envelope shift vs the source (formant movement), onset sharpness
(transient smearing), stereo correlation/width vs source, and render time.

**Decision rule.** Per family and offset band (±12 vs ±24), pick the engine with the
best blind preference; keep resampling wherever it ties (simplest wins). The likely
outcome is a hybrid (resampling near the root, formant-aware further out), which the
Register Engine can blend.

## Reproduce

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
R=./build/apps/research-renderer/research-renderer
$R --corpus research/corpus --profile standard --engines A,B   # after copying the corpus in
$R --benchmark
```
