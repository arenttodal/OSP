# Architecture

Phase 0 deliberately kept the system small: a pure-C++ DSP/analysis library, a thin
JUCE I/O layer, a research library, and a CLI. Phases 2–8 added the instrument engine
("engine C", `src/engine/`) on the same foundations; the plugin and the research tools
play exactly the same code. See "Instrument engine" below.

```
                   ┌──────────────────────── osp_research (static) ─────────────────────────┐
 research-renderer │ RenderSession  RenderMetrics  CorpusIndex  CorpusRunner  Benchmark      │
 (CLI)  ─────────► │ SourceAnalysis  Fixtures  RenderConfig  TestSignalSet                   │
 osp_tests ──────► └───────┬───────────────────────────────────────────┬─────────────────────┘
                           │                                           │
            ┌──────────────▼──────────────┐             ┌──────────────▼─────────────────────┐
            │ osp_dsp (static, pure C++20)│             │ I/O sources (compiled per target)  │
            │ core/      Prng, PitchMath, │             │ io/AudioFileIO   (JUCE formats)    │
            │            Fft, Statistics, │             │ io/ContentHash   (JUCE SHA-256)    │
            │            AudioData        │             │ io/JsonUtil, io/AnalysisJson (JSON)│
            │ audio/     sampler, voices, │             │ midi/MidiFileIO  (JUCE MidiFile)   │
            │            pitch, envelopes,│             └──────────────┬─────────────────────┘
            │            utility          │                            │
            │ analysis/  pitch, onset,    │             ┌──────────────▼─────────────────────┐
            │            spectrum,        │             │ osp_juce_headless (static)         │
            │            loudness, stereo │             │ juce_core, juce_audio_basics,      │
            │ model/     AnalysisData,    │             │ juce_audio_formats, juce_cryptography│
            │            PlaybackSource   │             └────────────────────────────────────┘
            │ midi/      MidiEvent,       │
            │            MidiFixtures     │
            └─────────────────────────────┘
```

## Module boundaries

### `osp_dsp` — pure C++20 (no JUCE, no I/O)

Everything that does signal processing or analysis lives here, so it can be unit
tested, run in the CLI, in a plugin, on a worker thread or on the audio thread
without dragging in a framework. Deviation from "use JUCE for everything": JUCE's
`dsp::FFT` and buffers would tie analysis to JUCE's module system and per-target
compilation; a 60-line radix-2 FFT and `std::vector` planar buffers are simpler to
reason about and portable. JUCE is still used for everything it is good at (file
formats, MIDI files, JSON, SHA-256, and later GUI/plugin hosting).

- **core/** `Prng` (xoshiro256**, deterministic), `PitchMath` (MIDI/Hz/cents/dB/note
  names), `Fft`, `Statistics` (weighted median, percentiles, periodicity), `AudioData`
  (canonical planar float audio).
- **audio/pitch/** `SincInterpolator` — Kaiser-windowed sinc reader whose kernel
  stretches when reading faster than 1:1 (anti-aliasing for upward transposition and
  downsampling). This *is* pitch branch "A — resampling".
- **audio/envelopes/** `Adsr`.
- **audio/voices/** `SamplerVoice` — one-shot playback of a `PlaybackSource`.
- **audio/sampler/** `BaselineSampler` — polyphony, velocity → gain, sustain pedal,
  voice stealing (released → quietest → oldest, with 5 ms fades into spare "tail"
  slots), optional naive randomisation (baseline B).
- **audio/utility/** `TestSignals` — deterministic ground-truth generators.
- **analysis/** `Analyzer` runs: `AnalysisFrames` (mono mix + 20 ms RMS on a 10 ms hop)
  → `PitchAnalyzer` (YIN) → `EnvelopeAnalyzer` → `SpectralAnalyzer` (STFT + flux) →
  `OnsetDetector` → `StereoAnalyzer`. All time series share the hop.
- **model/** `AnalysisData` (immutable, versioned result), `PlaybackSource`
  (immutable, zero-padded playback copy of the audio + root).
- **midi/** `MidiEvent`, `MidiFixtures` (frozen standard sequences + profiles).

### I/O layer (JUCE)

`src/io` and `src/midi/MidiFileIO.cpp`. These sources are compiled into
`osp_research` for headless tools (linking `osp_juce_headless`, which compiles the
needed JUCE modules once with console-app settings). Plugin targets will compile the
same sources against their own JUCE configuration. Reasoning: JUCE modules must be
compiled with per-target preprocessor settings, so a plugin cannot link a JUCE
library built for a console app.

### `osp_research`

Renderer and corpus logic as a library so that the CLI and the tests exercise exactly
the same code paths.

- `SourceAnalysis::loadAndAnalyse` — load + hash + analyse, never throws.
- `RenderSession::chooseRoot` — override > detected > low-confidence estimate > C4,
  always labelled. `renderSequence` — block-based render identical to host processing.
- `RenderMetrics` — safety checks (NaN/Inf, clipping, DC, silence, channel count,
  sample rate) + summary metrics + isolated-note pitch checks + sample hash.
- `CorpusIndex` / `CorpusRunner` — scan, hash, analyse, render fixtures, summarise;
  per-file failure isolation; optional parallel workers.
- `Benchmark` — per-callback timing of the sampler.

## Data flow

```
file ──decode──► AudioData ──Analyzer──► AnalysisData ──chooseRoot──► root
                    │                                                  │
                    └────────────► PlaybackSource (padded, immutable) ◄┘
                                          │
                     MidiSequence ──► BaselineSampler.render() per block ──► AudioData
                                                                               │
                                                                 RenderMetrics + WAV
```

## Thread safety (designing for the plugin now)

| Thread | Allowed | Phase 0 code |
|---|---|---|
| Audio | voices, prepared DSP, MIDI handling; **no** alloc/locks/I/O/analysis | `BaselineSampler::noteOn/noteOff/render`, `SamplerVoice`, `Adsr`, `SincInterpolator::computeKernel/apply` |
| Message/UI | parameters, drag & drop, starting analysis | — (Phase 1) |
| Analysis worker | decode, analyse, build `PlaybackSource` | `loadAndAnalyse`, `Analyzer`, `PlaybackSource` ctor |

Rules already enforced by the design:

- `BaselineSampler::prepare()` allocates (interpolator tables, voice state); nothing
  after it does. Voices are a fixed `std::array`; the kernel scratch is inline.
- `PlaybackSource` is immutable after construction; voices hold a const pointer.
  For the plugin, a new source will be built off-thread and handed over atomically;
  the old one must stay alive until no voice references it (voices "finish against
  the previous model", spec §59). That hand-over is Phase 1 work.
- `setSource()` is a pointer store; it does not free anything.

## Determinism

- All randomness comes from `osp::Prng`, seeded per note from
  `deriveSeed (configSeed, noteOnCounter, note)`. Same config + same MIDI → same output.
- Rendering is sample-accurate and independent of block size (tested: blocks of 32,
  64, 128, 512 and 1024 samples give bit-identical output).
- The corpus runner's results do not depend on the number of worker threads.
- Bit-identity across compilers/platforms is **not** guaranteed (libm differences);
  golden tests compare robust metrics and treat the stored sample hash as
  informational.

## Plugin (Phase 1)

`apps/plugin` builds AU/VST3/Standalone with `juce_add_plugin`. It links `osp_dsp` and
compiles the I/O sources with the plugin's own JUCE settings.

```
 drop / Load… / state recall ─► loader thread (juce::ThreadPool, 1 thread)
        message thread              │  hash → SampleStore import → decode → analysis (cached)
                                    │  → PlaybackSource + waveform overview → LoadedInstrument
                                    ▼
                  finished-loads queue (mutex: loader ↔ message thread only)
                                    │  20 Hz timer
                                    ▼
             ModelExchange::publish ──(atomic pointer)──► audio thread: takePending()
             collectGarbage()  ◄──(atomic generation)── publishOldestInUse()
```

- **Audio thread** (`processBlock`): keyboard-state merge, instrument swap, parameter
  changes (ADSR, gain, velocity range, pitch offset), sample-accurate MIDI (note on/off,
  sustain, pitch bend, all-notes-off), `BaselineSampler::render`. No allocation, locks or
  I/O. When a new instrument arrives, the previous one is kept in a fixed array of 8
  retired slots until no voice reads it, so held notes finish on the old sample.
- **Message thread**: parameters (`AudioProcessorValueTreeState`), editor, state,
  publishing loaded instruments and freeing retired ones (`ModelExchange`).
- **Playback preparation** (`model/PlaybackPreparation`): notes start just before the
  analysed onset and each source is level-matched (max RMS −16 dBFS, peak-limited), both
  non-destructive and baked into the immutable `PlaybackSource` off the audio thread.
  Off by default in the research baselines A/B; on in the plugin.
- **Root override** is applied as a pitch offset (`analysisRoot − userRoot`), so changing
  the root never rebuilds playback data and affects sounding notes immediately.
- **Sample store** (`SampleStore`): imported files are copied once as
  `<sha256><ext>`; analysis is cached as `<sha256>.analysis.json` and reused unless the
  analyser version changed. Session state stores hash, file name, original path and the
  root override — never audio.
- **State** (`getStateInformation`): APVTS parameters + an `Instrument` child, plus
  `stateVersion`. The child also stores the playback model actually used (root, root
  origin, start offset, playback gain) as round-trip-exact text, so a reopened project is
  bit-identical even if analysis improves later. Recall looks up the store by hash, falls back to the original path,
  and warns if the content changed.

## Instrument engine (Phases 2–8)

```
 AudioData + AnalysisData ──InstrumentBuilder (worker thread)──► InstrumentModel (immutable, staged)
                                                                   stage 1 provisional: PlaybackSource + profiles
                                                                   stage 2 continued:   ContinuationModel, resonances
                                                                   stage 3 complete:    register anchors (Natural)
 several files ──inferSampleSet──► InstrumentSet (members = models; groups, layers, round robins, register model)

 note-on ─► InstrumentEngine (audio thread, no allocation)
              member choice (set): nearest pitch group -> velocity layer -> round robin (never the previous take)
              NoteShape = velocity level + applyDynamics (DYNAMICS) + PerformanceEngine (LIFE, memory)
                          + MOTION drift + Reimagined (segment length, saturation) + register brightness
          ─► InstrumentVoice: sinc read of the chosen PitchLayer (Tape = original, Natural = nearest anchor)
                              continuation random walk over jumps, correlation-aware crossfades
                              release graft into the recording's own ending
                              transient swap: separated onset transient at its own speed (spec §19)
                              shelves (brightness/body), transient, damping, expression (pressure, MPE)
          ─► PostProcessor: CHARACTER (body resonances moved + tilt), sympathetic resonators (Reimagined),
                            SPACE (width, decorrelation, FDN ambience)
```

- **Modules.** `analysis/continuation/` (stable region, jump points, graft exits),
  `model/ContinuationModel`, `model/InstrumentModel`, `model/InstrumentSet`,
  `engine/InstrumentBuilder` (stages, sets, calibration, resonances),
  `engine/InstrumentEngine` + `InstrumentVoice`, `engine/PerformanceEngine`,
  `engine/PostProcessor`, `engine/SampleSetInference`, `engine/ShelfFilter`,
  `analysis/transient/` (onset HPSS, spec §19).
  All in `osp_dsp` (pure C++; Signalsmith Stretch is linked privately for anchors).
- **Why a new engine next to `BaselineSampler`**: baselines A and B must stay exactly as
  they are (Rule 7, every listening test compares against them; golden renders cover
  them). Engine C shares the interpolator, ADSR and PRNG.
- **Transient/body separation (spec §19).** Stage 2 runs HPSS-style median filtering
  over the first half second of every pitch layer and keeps the broadband part as a
  short `PlaybackSource` on the layer's own frame grid. A note far from the root adds
  `transient(read at its own speed) − transient(read at the body's speed)`, both aligned
  on the transient's peak. Playback stays a pair of linear reads (no audio-thread
  analysis), sources without a transient are unaffected, and the swap is exact where
  nothing is transposed.
- **Real-time safety.** Everything that allocates (analysis, continuation search, anchors,
  set inference) runs in the builder on a worker thread; the engine, voices and post
  stage allocate only in `prepare()`. Voices hold raw pointers into immutable models;
  the plugin keeps retired instruments alive until `isModelInUse` / `isSetInUse` say no
  voice reads them.
- **Staging (spec §62).** The plugin publishes each stage as soon as it exists: the
  instrument is playable after stage 1 (plain sampler behaviour), gains endless sustain
  with stage 2 and Natural pitch with stage 3. Refinements of a superseded sample are
  skipped. Sets are built with continuation in one job (no anchors: real recordings
  are the register).
- **Determinism.** Note shapes, continuation walks, drift and round-robin choice derive
  from `deriveSeed (seed, noteCounter, note)`; the performance memory depends only on
  the event sequence. The plugin restarts performance memory at transport start, so a
  bounce from the same position repeats exactly. Control-rate state lives per voice
  and in the post stage, which starts every `prepare()` from the current macro targets
  (no history), so output is identical across block sizes, and a recalled session is
  bit-identical to the original (tested for single files, sets and imported packages).
- **Recall.** A fresh load builds its model from the analysis after a JSON round trip,
  i.e. exactly what the cache returns on recall; single-file sessions also store the
  exact playback root/start/gain. Sets store member hashes and user assignments and are
  re-inferred from the cached analyses.
- **Plugin parameters** (state v2/v3): the five macros and Original ↔ Reimagined,
  Pitch Character, Sustain, Variation Seed, MPE, plus the Phase 1 advanced set. v1
  sessions migrate to neutral settings so they keep sounding like the sampler they
  were made with. Starting states are host programs. MIDI: velocity, sustain, pitch
  bend, mod wheel (MOTION), aftertouch / channel pressure (intensity), CC74 (timbre),
  CC 20–25 (macros), MPE lower zone (per-note bend ±48 st, pressure, slide).

## Recorded deviations from the suggested layout

- `src/research/` added: the renderer logic is a library so tests share it.
- `src/presets/` not created: presets are the plugin's state XML (`.osppreset`) and
  portable instruments are a zip of state + sources + analyses (`.ospinstrument`),
  both implemented in the plugin processor; no separate preset model was needed.
- `src/engine/` added for engine C (the spec's layout has no slot for it).
- `apps/standalone/`: the standalone app comes from the JUCE plugin target
  (`juce_add_plugin(... FORMATS AU VST3 Standalone)`), so there is no separate app.
- `RootChoice` (root selection + character description) lives in `src/model` because
  the renderer and the plugin must agree on it.
- Pitch branch A lives in `audio/pitch/` (the resampler is the first pitch engine).
- `tests/support/` holds shared test helpers.
