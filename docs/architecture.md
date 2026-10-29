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
              NoteShape = velocity level + applyDynamics (DYNAMICS) + PerformanceEngine (LIFE, memory;
                          CHARACTER loadings/spreads; TAKES: fixed stratified takes per note, recomputed from the seed)
                          + MOTION drift + Reimagined (segment length, saturation) + register brightness
          ─► InstrumentVoice: sinc read of the chosen PitchLayer (Tape = original, Natural = nearest anchor)
                              continuation random walk over jumps, correlation-aware crossfades
                              release graft into the recording's own ending
                              transient swap: separated onset transient at its own speed (spec §19)
                              shelves (brightness/body), transient, damping, expression (pressure, MPE)
          ─► per layer: ReimaginedStage (per-layer routing only; resonators + formants on the layer's own signal)
          ─► mix: weight x LEVEL x PAN per layer, summed
          ─► PostProcessor: ReimaginedStage (legacy routing only, at the layers' power-weighted amount),
                            DRIVE (bypassed at 0 %), MOVEMENT bus, SPACE and ECHO (parallel sends)
          ─► output gain (VOLUME)
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

## REIMAGINED modes

Each layer's REIMAGINED has a mode (`ReimaginedSettings` in `LayerSettings`): KALEIDOSCOPE,
TAPE FRAME, TOYBOX, MOSAIC, MIRAGE. Report: `docs/reports/reimagined-modes.md`.

```
 stage 2 (worker): analyseReimagined -> ReimaginedAnalysis (immutable, in the model)
                   TAPE FRAME splice map from the continuation's matched jumps (no audio copy)
                   MOSAIC 16-32 harmonic frames: 48 partials, inharmonicity, 6 residual bands
 note-on: the layer's mode (and amount > 0) picks the voice's engine; the voice keeps it
 voice:   KALEIDOSCOPE   native read path + shapeFor's per-voice part (unchanged)
          other modes    renderMode(): ReimaginedVoiceEngine::render (n <= 32) between the
                         voice's control updates, then the same envelope, shelves, CHARACTER,
                         level and pan
 layer:   ReimaginedStage only for KALEIDOSCOPE layers (others fade it out)
```

- **One contract** (`engine/ReimaginedEngine.h`): `prepare / start / release / control /
  stepFactor / wantsDryRead / render / finished`. TAPE FRAME, TOYBOX and MIRAGE read the
  recording themselves; MOSAIC takes the voice's plain read (`dry`) while it crossfades
  into its reconstruction, and stops asking for it once it has taken over. In Granular
  mode the grains are the engines' input.
- **Why KALEIDOSCOPE stays the voice's own path.** It is the algorithm every older patch
  was made with; moving it behind the interface would risk a sonic change for no gain.
  Its amount mapping moved unchanged into `KaleidoscopeEngine.h` (null-tested sample for
  sample); FOCUS and SPREAD are exactly neutral at 0.5.
- **Preallocated.** One instance of each engine lives inline in every voice (~7.8 KB of
  its 16.6 KB); `start()` returns false when the mode's analysis is missing or failed and
  the note plays the recording. A note at 0 % gets no engine at all (exact original path).
- **Switching** applies to new notes; sounding notes keep their engine. Sub-settings and
  the amount are read live at control rate from the slot's `ReimaginedLive`.
- **Plugin.** Per layer, version hint 11: `layerX.reimagined.mode` and
  `layerX.reimagined.<mode>.<setting>` (15 per layer). State 9; older states load
  KALEIDOSCOPE at its defaults. A gesture on any of them converts a legacy-routed patch
  to per-layer routing, like the amounts. The popover (`ReimaginedPanel`, MacroPopups.cpp)
  opens from the card's REIMAGINED name (editor popup 7 + layer).

## Adaptive layers (1–3) and the Granular source mode

`InstrumentEngine` holds three source layers (A, B, C) in a fixed `std::array<Slot, 3>`:
model or set, root-correction × TUNE pitch ratio, round-robin memory, performance engine,
live granular settings (START added to POS, REVERSE, FOLLOW), render buffers, the grain
snapshot and the gains applied last block. Every note-on starts one voice per occupied
layer (same event index for A, salted ones for B and C, so each layer's randomness is its
own while the shared seed keeps the performers' slow state together); polyphony is counted
per layer, the musical voice count per note-on. A layer's `LayerSettings` (START, TUNE,
PAN, LEVEL, REVERSE, LOOP, FOLLOW) act in its voices (START/REVERSE/LOOP/FOLLOW) or at its
mix gain (TUNE via the pitch ratio, PAN and LEVEL).

The mix is one pure function, `mixWeights (occupied, blend, x, y)`: one occupied layer
plays at unity (wherever the controls are — a single sound is never half of a mix); two
use the equal-power A/B blend (`cos`/`sin`, exactly the old two-layer law, so A/B sessions
are bit-identical); three take their barycentric share of a triangle position as power
(`sqrt`), so the total power is constant. The controls are smoothed over ~20 ms; each
layer's L/R gain (weight × LEVEL × PAN balance) glides block to block and no jump is
faster than 10 ms. A layer whose gain stays at zero for a block is not rendered (CPU):
its held voices wait, released ones and those of an emptied layer end. Then the
**shared** post stage (Reimagined resonators and formants, MOVEMENT bus, SPACE) runs once
— there are no per-layer macro chains; the per-voice stages (LIFE, DYNAMICS, CHARACTER
filter, drift, the ADSR) are the same settings in every voice.

Original <-> Reimagined per layer: `LayerSettings::reimagined` (below 0 = the instrument's
`Macros::reimagined`) shapes that layer's notes (continuation segments, saturation, doubling,
Reimagined grains, its own drift); the shared post stage (resonator bank, width, formants)
takes the layers' amounts weighted by their power in the mix (only when a layer has its own).
In the plugin layer A's amount is the original `reimagined` parameter; B and C have
`layerB.reimagined` / `layerC.reimagined` (state v7: older sessions give them A's amount).
`reimaginedLink` is kept in the state for old sessions but no longer used (the central
track is gone; REIMAGINED is a knob in each card and follows the layer LINK).

**Reimagined routing (state v8).** Reimagined has a per-voice part (continuation segments,
saturation, doubling, grains, drift), always per layer, and a bus part, `ReimaginedStage`
(one implementation: resonator bank tuned to the source, a bank a fifth above, two wandering
formants). `EngineSettings::reimaginedRouting` says where the bus part runs:

- `legacyGlobal` - the engine default and every session, preset or starting state saved
  before per-layer routing: ONE stage in PostProcessor after the mix at the layers'
  power-weighted amount, exactly as before (reference renders in
  `tests/audio/reimagined-before/` stay sample-identical). The layers' stages never run.
  `Reimagined(A + B)` is not `Reimagined(A) + Reimagined(B)` (shared resonators excited by
  the mix, one formant path), which is why old patches cannot simply be re-routed.
- `perLayer` - new patches: each layer slot runs its own `ReimaginedStage` on its own
  rendered signal before LEVEL, PAN and the mix (own resonances from its own model, own
  formant path); PostProcessor's stage is held at 0. A stage at 0 % is skipped; above 0 it
  runs every sample, also between notes (tails ring out, output stays block-size
  independent). Cost: about 1-2 % of a core per active layer at 48 kHz.

The resulting chain per note: SOURCE -> One Shot / Granular -> START / TUNE -> per-voice
LIFE, DYNAMICS, CHARACTER filter, Reimagined per-voice part, drift, ADSR -> (per layer)
REIMAGINED stage -> LEVEL / PAN -> mix -> DRIVE -> MOVEMENT bus -> SPACE and ECHO (parallel
sends, see below) -> VOLUME. LIFE and the
other macros are unchanged and stay shared (their per-voice stages were already per voice).

The plugin stores `reimaginedRouting` ("perLayer" / "legacyGlobal"); a state without it
(stateVersion < 8) is legacy. `OspAudioProcessor::convertLegacyReimaginedToPerLayer()` is the
only switch from legacy: a gesture on one of the three REIMAGINED parameters (the knob, its
double-click reset, LINK following it, a host's control surface); never loading, opening the
editor, attachments or automation playback (which sends no gestures). The amounts are kept.
Fresh instances, INIT and Reset settings are per layer with every REIMAGINED at 0 %; the
factory starting states set legacy (they were made with the shared stage).

Kept slots: "Clear all samples" and user starting states (`.ospstate`, settings + slot count,
no audio) keep empty A/B/C slots. The session stores `keptSlots`; the editor shows a card per
slot (`slotCount()` = the highest layer with a sound or the kept slots, whichever is more), a
sound added or dropped into a kept slot keeps its layer controls, and the engine's mix law
counts kept slots (`InstrumentEngine::setMixSlots`), so a refilled layer plays at the share it
had (empty slots are silent and take no part in the per-layer Reimagined weighting).

Source modifiers in the voice: START offsets the read (from the end when reversed) with a
3 ms fade-in; REVERSE reads backwards and so does everything that follows the read head:
the continuation walk takes every jump mirrored (it leaves at the end of the jump's `to`
window and lands at the end of its `from` window, so the crossfade covers the same matched
audio), Reimagined's doubling head trails at a later frame and its grains come from what
the note has already played (later in the file) and play backwards; only the release graft
and the transient swap stay off (the recording's ending is where a reversed note begins);
LOOP off plays the recording once (and, as the
old "Recording" sustain did, without per-voice drift); FOLLOW off multiplies by a lift taken
from the analysis' 20 ms RMS series (`levelContour::boostDb`: towards the loudest level, at
most +24 dB, none near the noise floor), glided per control period. Granular applies
REVERSE and FOLLOW per grain.

The ADSR is the instrument's one envelope (`attack`, `decay`, `sustainLevel`, `release`);
every voice runs it, so note stealing and release grafts keep working. A sustain level
changed during a held note glides there over ~10 ms.

Granular is a **source mode**, not an effect: `GranularSource` lives inside the voice
and replaces the read-through (continuation, grafts, doubling, Reimagined grains and the
transient swap are off for that voice). It plays Hann-windowed grains (window by
rotation, no per-sample trig) from a fixed pool of 24, read with cubic interpolation at
the voice's current step times TUNE, so bends and drift still apply. Grain positions are
POS ± SPREAD × half the recording, clamped so reads never leave it. For the display the
engine publishes every playing grain (position, window level × envelope, a random lane)
into a per-layer snapshot of relaxed atomics after each block; the editor reads it at
30 Hz and draws the cloud over a cached waveform image. After note-off
no grain starts and the voice ends when the last grain does (or the release ends). The
mode is fixed per note (switching never clicks); POS/SIZE/DENS/TUNE/SPREAD are read live.

In the plugin each layer is a `Layer` (model exchange, playing/retired instruments, load
state, root override, undo history). A layer is *occupied* while it holds or loads a sound;
the editor derives its layout from that alone (0: a drop zone, 1–3: equal `EngineCard`s in
Hero / Dual / Triple density) and never stores geometry. `addLayers` fills free slots (one
file each, extra files reported), `removeLayer` compacts (the complete state of the layers
above moves down: sound, root, mode, granular and layer controls) so occupied slots stay
contiguous; one removal can be restored. LINK is a message-thread behaviour
(`applyLinkedDelta`): a user gesture on one linked layer's START/TUNE/PAN/LEVEL moves the
other linked layers by the same amount. State v6 stores A in `Instrument`, B in
`InstrumentB` and C in `InstrumentC`; older sessions migrate (see `applyStateXml`).
Existing automation IDs (`layerA.*`, `layerB.*`, `ab.blend`) are kept; layer C and the
new controls follow the same scheme (`layerC.*`, `layerX.start/tune/pan/level/link/
reverse/loop/follow`, `mix.x`, `mix.y`, `decay`, `sustainLevel`).

The macro popups draw their pictures from the DSP's own pure functions and parameters
(`mixWeights`, `triangleShares`, `SpaceReverb::portrait`, the CharacterFilter response
designs, `shaping::*`, `RhythmicShaper::evaluate`, the shaper's atomic phase) and from data
prepared off the audio thread (each sound's waveform peaks, RMS and centroid series and an
averaged log-frequency spectrum computed by the loader). Their timers run only while a
popup is open; the audio thread only publishes atomics (grains, read heads, shaper phase,
the last 16 velocities).

## MOVEMENT v2 and SHAPER

MOVEMENT runs on the mixed instrument (after the layer mix and the per-voice stages,
before SPACE) in `MovementBus`; DRIFT's per-note part stays in the voices. Each mode keeps
its own settings in `Shaping` (stable IDs `movement.<mode>.<setting>`); switching modes
crossfades the two outputs for 60 ms. The main knob keeps its original ID `motion`.

`RhythmicShaper` (SHAPER) evaluates one of 12 compiled-in 16-step patterns (start/end value
and a shape per step; SMOOTH rounds the shapes and widens the hand-over between steps) at
a phase taken from the host: the plugin builds one `HostTiming` per block (playing, BPM,
PPQ, time signature) and the shaper advances PPQ per sample from it, so step edges land on
the right sample at any buffer size and every block re-anchors to the host (loops, jumps,
bounces). With the transport stopped a local clock starts on the first note-on (signalled
by the engine at the exact sample) and stops after 1.5 s without voices. Depth = the
MOVEMENT macro: VOL scales the dip; FILTER closes its own LP12 (TPT SVF, Q ~0.77, log
cutoff 18 kHz .. 380 Hz with a 0.8 exponent on depth); BOTH uses the full filter range and
0.55 of the volume dip. 2 ms one-pole ramps make every edge click-free, pattern and rate
changes crossfade for 30 ms, target changes for 40 ms, and zero depth is an exact bypass.
The UI reads the shaper's phase through an atomic (`InstrumentEngine::shaperPhase`); the
pattern strip draws with the same pure `RhythmicShaper::evaluate` as the DSP.

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


## SPACE v2 and ECHO (state 10)

`PostProcessor` sends the post-MOVEMENT signal to two effects in parallel, each with its own
macro as the send level: `SpaceReverb` (SPACE) and `EchoDelay` (ECHO). Neither feeds the other.

- `SpaceReverb::Settings` splits into structural settings (type, size: the network's lengths)
  and tuning (decay, pre-delay, damping, modulation, width, EQ). A structural change
  crossfades to the second, freshly configured instance (250 ms; a moving SIZE waits until it
  rests); tuning changes `tune()` the playing one in place. ROOM, HALL and PLATE share one
  late network (stereo input diffusion, 8 lines with an allpass each, cubic moving reads,
  two-band decay); ROOM's early reflections come from a shoebox's image sources, HALL's from a
  designed lateral pattern; SPRING keeps its dispersive tank.
- `EchoDelay` is a tape or BBD delay (see the class comment); it sleeps like the reverb once
  its line is empty below -120 dBFS.
- Both are real-time safe (prepare() allocates) and deterministic (seeded wander).

SHAPER CUSTOM: `ShaperParams::custom` / `customSteps` carry the musician's 16 steps (the same
`ShaperStep` form as the library). The plugin keeps them on the message thread
(`OspAudioProcessor::setShaperCustomPattern`, undoable), publishes them as 48 atomics plus a
generation counter, and the audio thread re-reads them when the generation moves; the state
stores them as `shaperCustom` ("v1:" + 16 x "start,end,shape"). Saved pattern files are JSON
(`schemaVersion` 1, `kind` "osp.shaperPattern", `steps` [[start, end, shape]...]).

Report and measurements: `docs/reports/space-echo-shaper.md`.

## DRIVE (parameters v14)

`DriveProcessor` is one new stage in `PostProcessor`, right after the legacy shared
REIMAGINED stage and before the MOVEMENT bus (so MOVEMENT, ECHO and SPACE hear the driven
sound). It is shared (the mixed instrument, once), not per layer or voice. At DRIVE 0 it is
not in the signal path (`process` returns at once), so sessions without DRIVE render
bit-identically; it engages and releases with a 10 ms fade and crossfades its circuits over
50 ms. Its nonlinear core runs 4x (2x from 88.2 kHz) through minimum-phase polyphase IIR
half-bands designed in `prepare()` (no latency is reported; it has about a sample of group
delay at mid frequencies), between exactly inverse first-order emphasis shelves. Output
compensation is a static, measured table per circuit and BODY. The DYNAMICS macro's
parameters (`dynamics`, `dynamics.curve`, `dynamics.tone`, `velocityRange`) and DSP are
unchanged; only their controls moved to Advanced. Report: `docs/reports/drive.md`.

## ARPEGGIATOR (parameters v15)

`engine/Arpeggiator` (pure C++) is a note-event stage in front of `InstrumentEngine`, not
part of it: the plugin's `processBlock` merges the screen keyboard, then
`runArpeggiator` gives note on/off, CC 64 and All Notes Off to it and lets everything else
pass at its sample; its events go into a `MidiBuffer` preallocated in `prepareToPlay`, and
the existing sample-accurate render loop plays the result. One arpeggiator for the
instrument (no per-layer copies), no DSP changed. While it is off it only follows the held
notes (for a clean hand-over when switched on) and the engine plays the MIDI exactly as it
arrived, so sessions from before it are bit-identical (13-scene null test). Timing: the
host's PPQ grid while it plays (resync on loops/jumps, no catch-up), free running at the last
tempo when stopped. Every generated note gets a scheduled note-off; because the engine
releases all voices of a pitch together, a pitch still sounding is ended right before it is
played again. RANDOM uses `osp::Prng` (variation seed + a phrase counter restarted with the
transport). The editor reads a 16-step display snapshot through atomics; its inline panel is a
view setting (`arpExpanded` on the state tree), never a parameter. Headless:
`research::arpeggiate` and the `"arp"` block of render configs. Report:
`docs/reports/arpeggiator.md`.

## MODULATION (parameters v17)

`engine/Modulation` (pure C++) is a control layer next to the DSP, not inside it: two LFOs
and two per-voice envelopes, a destination registry (`mod::destInfo`: owner, domain, span,
update policy; append-only) and 16 routes compiled into flat per-destination term lists.
The plugin turns its `mod.*` parameters (16 fixed route slots: automatable, saved, undoable)
and the custom curves (state tree `modCurves`, published to the audio thread through atomics
plus a generation counter) into one plain `mod::Settings` value on change. The engine never
writes a stored parameter: with a route active, `render()` runs in 32-sample chunks, moves the
global LFOs (host PPQ when playing) and hands the post chain `base + modulation` for the macros;
each voice keeps a `VoiceState` (its poly LFOs and envelopes) and adds its offsets where each
setting is used (gain ramped, ADSR times at note-on / note-off). With no active route
`render()` is the unchanged single-block path, so sessions from before are bit-identical.
A per-voice source never reaches a shared stage (no aggregation rule yet). GUI (refined; the first
version's window-widening bay was removed so the plugin keeps its size): `ModulationPanel`
turns the AMP envelope's panel into a five-tab editor (AMP, ENV 1/2, LFO 1/2; one shown at a
time; the tab is a view setting `modTab`), whose tabs are also the drag handles; compact
popovers hold the routes (`ModRoutesPopup`) and a large curve editor (`ModCurvePopup`).
`ModulationOverlay` is the reusable two-layer halo: it finds modulated controls by their
`paramId` property, sizes each ring from the knob's own reach (overridable per slider with
`haloGap` / `haloAvoid`), edits depth without touching the base, and takes the drops. One
selection (source, route) lives in the editor and drives tabs, popover and halos.
Refinement (ANDOR/OSP): five sources (MOD WHEEL appended) and appended destinations (ECHO,
START A/B/C read at note-on, the envelopes' own times). The shared stages hear `Runtime::
sharedValue`: the single-valued sources plus one global instance of each envelope (restarted
by each note-on, released by the last key and pedal). Voices hear `globalValue` and their own
envelopes and poly LFOs. Every assignment (drag, right-click, popover) goes through
`OspAudioProcessor::assignModulation` (rules, `mod::createsCycle`, duplicates, capacity).
Meta-modulation: a route may move another route's depth (`Dest::routeDepth`, `Route::target`,
one level, `mod::depthRule`). `Compiled` keeps each term's depth routes beside it and
`Compiled::depthOf` sums them, clamped, wherever the term is read (so scope and timing follow
the target's destination; without them the stored depth exactly). The plugin links routes by
stable IDs in the state's `ModLinks` child and resolves them to slots on the message thread
(atomics to the audio thread); `assignDepthModulation` is the same path for depths, and
removing a route removes its depth routes in one undo step. `ModulationOverlay::hitAt` tells a
halo's ring (a route's depth) from the knob (its value). Headless:
the `"modulation"` block of render configs. Report: `docs/reports/modulation.md`.

## PER-LAYER EQ (parameters v18)

`engine/LayerEq` (pure C++): five state-variable sections per layer, one processor per layer in
`InstrumentEngine::Slot`, run on the layer's summed buffer after its voices and its own
REIMAGINED stage and before the mix weight, LEVEL and PAN (the same place under legacy
REIMAGINED routing, whose shared stage then hears the equalised layers). Its settings live in
`LayerSettings::eq` (the plugin's `layerX.eq.*`, the research `"eq"` block); the engine adds
the global LFOs' EQ routes (`effectiveEq`) every 32-sample control chunk, and the processor
glides to them. Off, it is not in the path (bit-identical). The editor (`EqEditor`) draws
`eq::responseDb`, the exact response of the running sections. Report: `docs/reports/eq.md`.

## FINAL MODULATION AND DAMPEN (state 12, parameters v19; docs/reports/mod-final.md)

Sources appended to `mod::Source` (saved by index): VEL and NOTE (per note; on a shared
stage the player's latest key, `Runtime::setInputNote`, which the processor feeds from the
keys while the arpeggiator owns the notes), CC 1-4 and PRESSURE (global, glided like the
wheel). VEL and NOTE go through their own `mod::Curve` (the same bounded, tension-shaped
curves as the LFO and envelope shapes); APPLY TO AMP (`Settings::velocityToAmp`) lets the VEL
curve replace DYNAMICS' Soft / Linear / Hard for the amplitude, and the presets reproduce
those three exactly. The panel has six tabs (AMP | ENV | LFO 1 | LFO 2 | VEL | NOTE); ENV 2
keeps working without a tab. The curves live in the state as `modCurves` "c3" (nine: LFOs,
envelopes, VEL, NOTE, DAMPEN A-C), published like before through atomics and a generation.
ARP destinations (shared): the processor reads the engine's last control period
(`InstrumentEngine::arpModulation`) before running the arpeggiator. RATE is a multiplier on
the step clock: off 1x the arpeggiator runs a phase accumulator in steps (no restart; at 1x
the plain clock, back on the host's grid at the boundary nearest the expected step); Stepped
latches musical ratios at a step. OCTAVES by modulation applies at the next cycle. DAMPEN
(`engine/Dampen`): per voice after CHARACTER at both voice tails, a high shelf (to -18 dB) or a
12 dB/oct low-pass whose amount is the layer's curve at the sounding key times AMOUNT (plus its
routes), glided at control rate and skipped at 0 (bypassed by default: bit-identical). The
editor is the EQ overlay's second page (`DampenEditor`, EQ | DAMPEN).

## Voice economy and the opaque display (docs/reports/cpu-three-layers.md)

`EngineSettings::voicesPerKey` (2): a key struck again on a layer fades its oldest voice past
the limit. `retireBelowDb` / `retireSeconds` (−90 dBFS, 0.25 s): each voice reports the peak it
added (`takeOutputPeak`). A voice whose key is up (`retirable`: released, or held only by the
pedal, never Granular under the pedal nor a REIMAGINED mode engine), heard below the threshold
after its layer's gain for that long, ends with the steal fade. `SourceDisplay` is opaque: it
paints its ancestors' own paint behind its corners from a backdrop image (`paintBackdrop`,
keyed by size, position, scale, skin and focus), so moving marks repaint only the display. It
shows at most `maxHeadsShown` read heads.

## Fast numerics in engine C (docs/reports/cpu-three-layers.md, step 2)

`core/Simd.h` is four float lanes (`simd::Float4`): SSE2 on x86-64, NEON on arm64 (both part
of those platforms' baselines, so no runtime dispatch), and a plain-array fallback. Lane sums
are taken in one fixed order. It serves only engine C's hottest loops, which differ from the
plain forms by float rounding:

- `SincInterpolator::readFast`: table kernel and apply in one pass, both channels, taps summed
  in four lanes. The baselines keep `computeKernel` + `apply`, so their goldens never change.
- `GranularSource::render` and KALEIDOSCOPE's grains: Hermite reads as four tap weights
  (`hermiteWeights`) on one 4-sample load, the window and pan folded into the weights.
- `CharacterFilter`: LP24's left and right ladders in two lanes of one pass, with the loop's
  constants (powers of G, the feedback reciprocal, the saturator's bias offset) computed in
  `setParameters`.
- `engine/FastMath.h`: `fastTanh` (a rational fit within 4e-7 of tanh) for KALEIDOSCOPE's
  saturation, and `Phasor` (sin by rotation) for the doubling's slow sway.

`tests/unit/FastNumericsTests.cpp` keeps each one against the form it replaced.

## RENDERED CONTROLS (tools/blender-assets; docs/reports/knob-phase1.md)

Controls can be drawn from offline renders instead of vectors.

**Generator.** `tools/blender-assets/` is a procedural, headless Blender pipeline (JSON
configs plus named material, lighting and camera presets). It writes one zip of PNG frames
per size into `shared/andor-ui/Resources/knobs/<name>/`. These are embedded as binary data.

**Plugin side.** `SpriteKnobRenderer`:
- decodes a frame when first shown;
- resamples it once to the exact physical pixels at the current window and display scale,
  then draws it 1:1;
- shares one asset per process.

**Integration.** `design::draw::knob` keeps the ticks, arcs and halos drawn and replaces only
the body and pointer when `KnobStyle::sprite` is set. A slider asks for one with the
property `sprite`.

**Skin.** A development knob skin (Cmd+Option+K, `OSP_KNOB_SKIN`) chooses original, pilot
(LIFE) or all macros (the default, as in the ceramic design). It is looks only and never
touches state.

## CERAMIC SKIN (docs/design/warm-ceramic-teak-style-guide.md)

The instrument's look is the design lab's chosen direction (09 Warm Ceramic + Teak): rendered
surfaces under the plugin's own drawing. Text, icons, values, waveforms, knobs' rings and
halos, the keyboard and the dark displays stay vector-drawn.

**Generator.** `tools/blender-assets/skin/`: `pieces.py` lists what is rendered and where,
`render.py` renders it with the design lab's studio, materials and calibrated exposure (the
direction's own config in `design-lab/blender/configs`, so nothing restates a colour or a
light), `pack.py` writes `shared/andor-ui/Resources/skin/ceramic-skin.zip` (manifest plus one image
per piece and level).

Each piece is rendered at its real place and size in the design canvas, so the light falls on
it as in the approved full render:
- the window (surround, housing, teak cheeks), opaque;
- the cards at the three layout widths, the panels;
- the keys per role and state (normal, hover, pressed, on);
- the badges, the fader cap and rail, the lens LEDs;
- the displays' bezels and the keybed, as overlays.

Slabs keep their contact shadow in the alpha (a shadow catcher of the surface below). A
recess is rendered with and without itself, and the difference becomes the least opaque
overlay that turns the plain surface into the recessed one.

**Plugin side.** `CeramicSkin` (`skin::Pieces`, one per process while an editor holds it):
- a piece is drawn as a nine-slice over the part's bounds: at its rendered size it is the
  render itself, at other sizes the edges and middle stretch;
- for the physical scale (window times display) it is composed once from its nearest level,
  resampled by area, then cached and drawn 1:1; the cache is bounded;
- a requested face colour scales a piece's light and shading (another panel colour from the
  same render); the shadow stays a shadow.

`design::draw::housing`, `raised`, `button` and `well` use the pieces while the look is
ceramic. Keys (`skin::KeyFace`) and raised faces (`skin::SurfaceFace`) are drawn by the
surface they sit on, so their shadows can fall outside their own bounds (a component cannot
paint there). The component then draws only its ink, and repaints with room for the shadow
when its state changes. A face that is animating is stretched while drawing instead of being
composed for every size in passing.

**Look.** `skin::look()`: ceramic (default) or classic (the vector drawing it replaced,
kept for comparison and as the fallback when the pack cannot be read). It is set by
`OSP_SKIN=classic|ceramic` and toggled with Cmd+Option+G. It is looks only: no parameter,
no state, nothing the audio thread reads.

## LIBRARY (in progress; docs/library/)

The Library indexes sounds, presets and templates in one local SQLite catalog
(`src/library`, `osp_library`: pure C++ on the system SQLite, WAL, shared by every instance).
It is an index plus user metadata, never the store of record for audio: the content-addressed
sample store keeps the sounds and presets refer to them by hash, so a project recalls without
the catalog (decision D-02). `osp_dsp` never links it, and nothing of it runs on the audio
thread. Architecture, contracts, schema and formats: `docs/library/architecture.md`,
`schema.md`, `preset-format.md`, `template-format.md`; progress in `requirements-matrix.md`.

## ANDOR DESIGN SYSTEM AND INSTRUMENT SHELL (andor-design-system/HANDOFF.md)

**Why.** Sister products (ANDOR Drums, Grain, Phrase and those to come) must look and behave
exactly like ANDOR OSP. A copy of the interface would drift; subclassing OSP's processor would
drag along its engine and Library. So the interface became a shared library that OSP itself
is built from.

**What moved.**
- `shared/andor-ui` holds, unchanged:
  - the components, the look (design tokens, look-and-feel, ceramic skin, sprite knobs) and the embedded assets;
  - the frame (canvas, painting, placement, keyboard, macro row, popover placement), extracted from OSP's editor;
  - the parameter schema (`AndorParameters`, OSP's layout and IDs);
  - the sample loader.
- `InstrumentHost` is the only thing the components know about an instrument. OSP's processor implements it.
- Behaviour that needs only the parameters and the state tree lives in `InstrumentHost`, once: modulation routes and links, the assignment rules, MUTE/SOLO, LINK, the mix when layers are added, the curve codec.
- OSP keeps its editor's composition, the Library, its presets and its engine. Its macro and
  REIMAGINED popovers (`MacroPopups`) are shared too since ANDOR ANALOG uses them: they read
  only `InstrumentHost` (including SHAPER's custom pattern, the tempo and the shaper's phase).
- `apps/plugin/CMakeLists.txt` adds the shared code with `andor_ui_add_to (OSP_Plugin)`.

**Guarantees.** Nothing about OSP changed:
- Its renders are pixel-identical to before the extraction (`[design-capture]` before and after).
- Its 350-parameter schema equals the listing from the unmodified build (`tests/plugin/golden/osp-parameter-schema.txt`).
- Its full suite passes.

**The shell.**
- `shared/andor-shell` builds a complete plugin around a product's `Engine` with `andor_add_instrument`: processor, editor, presets, state, a diagnostic engine.
- Sources reach the audio thread through `ModelExchange`, as in OSP.
- The editor disables every control whose parameter the engine does not read (`Binding.h` tags, which are non-visual).
- Products live in `products/`. They are written by `tools/create_andor_instrument.py` and checked against `andor-design-system/product-registry.json`.
- Each product is tested by `andor_<slug>_tests` and compared with OSP, pixel for pixel, by `andor_<slug>_tests_visual`.

## KEY DNA (unified engine Phase 1; docs/research/unified-andor/phase-1-key-dna.md)

A per-layer layer on the existing Granulator, not a second engine. `keydna::Settings` per layer
(curves as OSP curve tables, depth, variation, Smooth <-> Individual, seed, AMOUNT) produce a
`keydna::Table` of 128 keys (computed in `InstrumentEngine::setLayerKeyDna`, allocation-free,
only when the settings change). At note-on a Granular voice copies its key's offsets into its
`NoteShape`; `InstrumentVoice::notesGranular()` applies them to the layer's own settings before
modulation, REIMAGINED scatter and LIFE, clamped to the knobs' ranges, untouched when zero.
The individuality draws from its own stream (`"keydna"` root), never from LIFE's or the grain
scheduler's. Off or neutral, the output is bit-identical to the engine without it (the Phase 0
baseline, tests/regression/golden/unified). Engine only: the plugin does not set it yet.

**Phase 2 (docs/research/unified-andor/phase-2-report.md) changed how KEY DNA reaches the
voices**, without changing what a profile is:
- the table holds every key's offsets at full AMOUNT (`keydna::Profile` = generation + table).
  It is computed off the audio thread and published as an immutable object:
  - the plugin computes it on the message thread and hands it over through a per-layer
    `ModelExchange<keydna::Profile>`;
  - the engine only swaps a pointer (`setLayerKeyDnaProfile`);
  - voices copy their key's offsets at note-on, so a superseded profile is free as soon as
    the next block starts.
- AMOUNT is live: `setLayerKeyDnaAmount` (the host parameter `layerX.keyDna.amount`) plus the
  `keyDnaAmountA..C` modulation destinations. Each voice follows it with a 30 ms control-rate
  one-pole, starts where it is (no glide in), and lands exactly on a still target.
  `keydna::apply (offsets, amount, params)` is the identity at 0 and the Phase 1 arithmetic at 1.
- `setLayerKeyDna (settings)` still exists: offline renders and tests use it, with storage
  owned by the engine.

## Granulator options and the grain budget (unified engine Phase 2)

`GranularParams` gains three options, all neutral by default:
- SCATTER (`scatterSemitones`): a per-grain random pitch within +-semitones.
- PAN: a per-grain random place within +-PAN of the field. It is equal-power, at unity in the
  centre; a stereo grain is balanced rather than summed to mono.
- WINDOW: HANN (the original raised cosine) or table windows built at load time and matched
  to Hann's energy (SOFT is a Tukey window; PERCUSSIVE is a quick rise and an exponential fall).

How the options stay out of the original path:
- SCATTER and PAN draw from the grains' second stream, `deriveSeed (seed, "grop")`, and only
  when they are non-zero, so the original grain choices never move.
- A panned or table-windowed grain is rendered by `renderShaped`. The original loop runs
  untouched while none plays.

The grain budget (`EngineSettings::grainBudget`, 0 = unlimited) is applied at the start of every
`InstrumentEngine::render`:
- Each sounding grain stream (a granular voice, and KALEIDOSCOPE's second stream) asks for
  `ceil (DENS x SIZE) + 1` grains.
- The share is the largest per-stream cap that keeps the total asked within the budget
  (max-min fairness): light notes keep every grain, and the densest are thinned alike. The
  integer remainder goes one grain each, in voice order.
- A budget that is not reached changes nothing (bit-identical).
- A thinned stream keeps the level it would have had: its norm is `sqrt (asked) x law (asked) /
  sqrt (admitted)`. The admitted count glides over about half a grain, so a share change does
  not step the level.
- The floor is one grain per stream.
- In the plugin, sessions saved before state version 13 open Unlimited; new sessions start
  at 256.

**Phase 3 made a finite budget a hard bound (`GrainBudgetPolicy`).** Under Phase 2's policy
(`shared`) the one-grain floor exceeds the budget when more streams sound than the budget has
grains (24 streams on a budget of 10 play at least 24 grains). The `bounded` policy, the
default for new engines and new plugin sessions, never exceeds it:
- an engine-wide `GrainGate {live, limit}` that every granular source checks before starting a
  grain. `live` is recounted at the start of each block and counts every grain started since;
  it never decreases within a block, so it errs on the safe side;
- with more streams than grains, the newest notes (by start order, then slot) get a cap of 1
  and the others 0. A cap of 0 holds the stream's normalisation, so its last grains fade out at
  their own level and nothing swells;
- the Granulator itself is unchanged. `shared` remains for sessions saved before state 14 and
  for the Phase 2 budget scenes, which pin it explicitly.

## NOTE ATLAS (unified engine Phase 3; docs/research/unified-andor/phase-3-report.md)

A recorded phrase becomes an ordinary OSP multi-sample instrument. There is no second sampler.

    finalised audio asset(s) ──► NoteAtlas (model/NoteAtlas, plain data, JSON io/NoteAtlasJson)
                              ──► atlasset::buildInstrumentSet (engine/AtlasSet, loader thread)
                              ──► InstrumentSet (members = region copies, groups = play keys)
                              ──► LoadedInstrument ──► ModelExchange ──► the layer's voices

- **The model** (`osp::atlas`, pure C++): sources are immutable assets identified by
  `contentHash` (sha256 of the stored file), with an origin (`fileImport`, `library`,
  `liveCapture`, `bufferSnapshot`). Paths are hints only, so a capture that never had a path is
  a first-class source. Notes carry exact frame regions (sourceStart <= attackStart <= sustain <=
  releaseStart <= sourceEnd), MIDI root + cents + confidence, velocity layer, round-robin
  group/variation, analysis or user origin, and corrections. `Mapping {fill,
  maxTransposeSemitones, correctTuning}` is FILL KEYBOARD. Schema 2; ANDOR Lab's schema 1 is
  read and migrated.
- **The conversion** copies each included note (attackStart..sourceEnd, its fades, a 64-frame
  anti-click edge at each cut) and builds one member per note with OSP's own builder
  (`buildProvisional` + `addContinuation`), its root forced to the play key (plus its cents
  when playing in tune), one playback gain for the whole atlas. Groups are play keys, layers
  are velocity layers, takes follow the variation order. `finaliseSampleSet` (shared with
  inferred sample sets) derives the dynamics and register models. Region copies keep grains,
  loops and releases inside their note by construction; each member still records its source
  asset and frame range, so a later shared-buffer implementation can read in place.
- **Selection** (`InstrumentSet::groupFor`): the exact key's group, else (FILL on) the nearest
  recorded key within `fillLimitSemitones` (ties: the lower key), else none. The engine's
  `memberFor` then picks the velocity layer and the round-robin take as for any set. An
  unmapped key starts no voice and steals nothing. Legacy sets keep an infinite limit and
  behave exactly as before.
- **Loading and publication** (`InstrumentLoader::loadInstrumentAtlas`, loader thread): every
  source is found by hash in the sample store, then through the atlas's hints; decoded once;
  checked against the atlas's rate / channels / length; kept in the store. Any missing or
  mismatched source fails the whole load. The result is published like every sound, so
  sounding voices keep the instrument they started on and it is freed on the message thread.
- **State 14**: the layer's `Instrument` tree holds an `Atlas` child (the atlas JSON plus each
  source's store hash), never audio. On recall the layer is emptied first, so a session whose
  recordings are missing opens empty and failed (`missingSources`), never with the previous
  sound.

## PHRASE ANALYSIS AND STAGED PREPARATION (unified engine Phase 4; docs/research/unified-andor/phase-4-report.md)

A monophonic recording (sung, bowed, played) is analysed into an ordinary schema-2 Note Atlas,
which then follows the Phase 3 path. Nothing new reaches the audio thread.

    immutable asset (decoded audio + SourceAsset: hash, origin)
        ──► phrase::AnalysisQueue (bounded workers, bounded queued audio, shared identical requests,
            cancel, progress) ── io::PhraseAnalysisCache (hash + analysis version + settings digest)
        ──► phrase::analysePhrase ──► NoteAtlas (notes + per-note diagnostics + the recording's verdict)
        ──► atlasset::buildPlayableSet  (Stage A: published at once)
        ──► atlasset::enhanceSet        (Stage B: continuation, published while notes may sound)

- **Analysis** (`src/analysis/phrase`, pure C++, ported from ANDOR Lab and adapted): decode,
  mono, high-pass, level / silence / noise floor; pitch (yin-viterbi: pYIN-style candidates and a
  Viterbi path; or plain YIN); segmentation (pitch changes, level-relative re-articulation,
  vibrato and tremolo vetoes, MIN NOTE consolidation of scoops, falls and octave slips); per-note
  description (stable pitch, cents, attack / sustain / release markers, fades, confidence);
  mapping (rounding, optional global tuning). OSP additions: per-note `Diagnostics` (uncertain
  pitch, possible octave error - including a spectral peak under the tracked pitch -, uncertain
  boundary, unvoiced, possible split - including short scoop-like entries -, quiet alternate);
  a note-level harmonic repair (relabelled only when a subharmonic spectral peak and a far deeper
  YIN dip at that period agree over the whole note); quiet alternates (a take more than 24 dB
  under the loudest take of its key stays in the atlas, off the keyboard); the recording's
  verdict (`suitability`, pitched share, uncovered pitched stretches = possible missed notes).
  `analysisVersion` 3. The analysis never sees the playback audio path and never runs in
  processBlock.
- **Entry point**: `analysePhrase (AudioData, SourceAsset, settings, cancel, progress)` takes any
  immutable asset; nothing assumes a file or a file picker (a buffer snapshot or a finalised live
  capture is the same call).
- **Corrections** (`phrase::edit`): set root, move markers, assign key, include/leave out, group,
  split, merge, re-describe a region. Each sets a `Corrections` flag. `mergeReanalysisReported`
  keeps every corrected note exactly, keeps ids of re-detected untouched notes (same place and
  key), gives new ids past every id ever used, and returns conflicts (`analysisDisagrees`,
  `noteGone`, `uncertainMatch`) instead of applying anything over a correction.
- **Staged preparation** (`engine/AtlasSet`): Stage A copies the regions, analyses them (in
  parallel when allowed), sets the shared gain and builds provisional members; Stage B adds
  continuation (sustain loops, release graft, transient layer, resonances, REIMAGINED data) per
  member on up to 8 threads. Both assemble the set through one function, so Stage B's set is
  bit-identical to a single-pass build, independent of the thread count (tested). A sounding note
  keeps the model it started with (tested sample-exact). The transient separation's median
  filters use an exact sliding median (bit-identical, about 3x faster).
- **Capacity**: 300 s of region audio per layer, 600 s for A+B+C together for new loads
  (`OspAudioProcessor::atlasCapacityFor`); a session's recall keeps the per-layer limit only, so
  it reopens as saved. A build over capacity, or with a note longer than 60 s, is `partial`, lists
  the skipped and cut note ids and warns; the atlas (and the session) keeps every note.
- **Plugin** (`analyseRecording`, `cancelAnalysis`, `atlasStatus`): import by hash, decode once,
  cached or queued analysis, Stage A published, Stage B queued behind it on the loader. A
  recording whose verdict is `unsuitable` or `silent`, or with no notes, is loaded as one
  ordinary sample instead, with the reason as the status message (kept across the later model
  stages of the same load). State stays 14: the analysed atlas is the layer's `Atlas` child.
