# Status

_Last updated: Phases 0–2 complete; Phase 3 (continuation) implemented and in the plugin, listening test pending; overnight run working through Phases 4–8._

## Milestone checklist — Phase 0: research harness + baseline engine

- [x] P0-01 Repository, CMake (no Projucer), JUCE 8.0.9 via FetchContent, Catch2 tests, docs
- [x] P0-02 Headless `research-renderer` (`--analyze`, `--source/--midi/--output`, `--analysis`, clear errors + exit codes)
- [x] P0-03 Corpus indexing (SHA-256 content ids, duplicates, unsupported files listed)
- [x] P0-04 Audio loader: WAV/AIFF/AIFF-C, mono/stereo(+), 44.1–96 kHz, 16/24/32-bit PCM, 32-bit float; non-destructive
- [x] P0-05 MIDI fixtures: repetition, dynamics, register, melody, chords, long-hold, long-chord, repeated-sustains (+ profiles)
- [x] P0-06 Baseline polyphonic sampler (A) + naive randomised sampler (B)
- [x] P0-07 Root/F0 analysis (YIN, weighted median, honest confidence, vibrato)
- [x] P0-08 Envelope/onset analysis (silences, onset, attack, peak, body slope/fluctuation, tremolo, secondary peaks, end state)
- [x] P0-09 Spectral analysis (centroid, rolloff, flatness, flux, periodicity, harmonic ratio, HF/LF energy + series)
- [x] P0-10 Stereo analysis (correlation, width, side/mid, balance, band widths)
- [x] P0-11 Versioned analysis JSON (documented in docs/analysis-schema.md)
- [x] P0-12 Corpus runner (profiles, engines, parallel jobs, failure isolation, summary.md/json)
- [x] Golden render regression system (8 cases) + render safety checks
- [x] Benchmark (`--benchmark`) + CTest performance smoke test
- [x] Run the real 80-file corpus: 80/80 analysed, 800 renders, 0 errors (`docs/reports/corpus-run-1.md`)
- [x] Optional minimal standalone/plugin (see Phase 1 below)

## Milestone checklist — Phase 1: playable sampler

- [x] JUCE plugin target: AU (macOS) + VST3 + Standalone, CMake only
- [x] Drag & drop / Load… / Load example (synthetic vowel until owned examples exist)
- [x] Background loading: hash → managed sample store → decode → analysis (cached) → playback data
- [x] Lock-free instrument hand-over to the audio thread with deferred destruction (`ModelExchange`)
- [x] Root detection shown; manual root override (pitch offset, no rebuild)
- [x] Waveform overview, character line ("SUSTAINED · TONAL · VIBRATO"), keyboard, voice count
- [x] MIDI: note on/off, velocity, sustain pedal, pitch bend (range parameter), all-notes-off
- [x] 24 voices, ADSR (attack/release automatable), velocity range, fine tune, output gain
- [x] Session state: parameters + sample hash/name/path + root override; recall from the sample store
- [x] Headless plugin tests (load, chromatic pitch, SR changes, root override, recall, bad files)
- [x] macOS 14 / Apple Silicon: build, all tests, `auval` (CI)
- [ ] Host validation: pluginval, Logic / Ableton / Reaper (needs a Mac)
- [x] Playback preparation: start at the analysed onset + level matching (plugin default; research option)
- [x] Session recall stores the exact playback model (bit-identical recall even if analysis changes)
- [ ] Exit check with real corpus samples in a DAW

## Milestone checklist — Phase 2: pitch engine bake-off

- [x] Offline pitch engines: A resampling, B Signalsmith Stretch 1.4.0 (MIT), C Signalsmith + formant compensation
- [x] Bake-off runner (`--bakeoff plan.json`): blind clips, common length, RMS-matched, `key.json` guard rails
- [x] Scoring (`--bakeoff-score dir --ratings file`): per engine / family / offset band
- [x] Run 1 rendered: 5 families x 5 offsets x 3 engines = 75 clips (`docs/reports/pitch-bakeoff-1.md`)
- [x] Blind listening page (`research/bakeoff/listening-page/`), ratings stored with the page
- [x] Listening ratings collected and scored (`research/bakeoff/pitch-bakeoff-1.ratings.json`)
- [x] Decision: A default, C rejected, B as "Natural" register anchors (`docs/reports/pitch-bakeoff-1.md`)
- [x] Natural pitch branch (offline register anchors, stage 3 of the model) in the plugin ("Pitch Character")

## Milestone checklist — Phase 3: continuation MVP

- [x] Sustain-region detection (one-shot for decaying/struck sources)
- [x] Loop candidates scored on spectrum, level + slope, F0 + slope; aligned by cross-correlation
- [x] Multi-loop random walk (never repeats immediately), correlation-aware crossfades
- [x] Movement: slow level / pitch / brightness drift (MOTION)
- [x] Release grafting into the recording's own ending
- [x] Experiment 2 (A naive loop / B best loop / C multi-loop / D + movement), 60 s holds, 5 sources
- [x] InstrumentEngine ("engine C") in the plugin; staged model (playable -> sustain -> anchors)
- [ ] Listening verdict (lab tab "Sustain") — exit condition: 60 s holds without an obvious loop

## Milestone checklist — Phase 4: performance MVP

- [x] Performance vector (level, brightness, body, transient, micro-pitch + settle, damping, start, stereo)
- [x] Correlation through latent factors (force, colour, timing) — not independent draws
- [x] Deterministic variation (seed + event sequence); transport start resets memory in the plugin
- [x] Performance memory (Ornstein-Uhlenbeck, 4 s)
- [x] Repetition awareness (alternating attacks, less pitch drift on fast repeats)
- [x] LIFE macro (0 identical, 0.5 realistic, 1 reinterpreted)
- [x] Calibration against the corpus' real repeated takes (Tagel, plucks; violin RR set has only 2 takes)
- [x] Experiment 3 (A identical / B independent / C LIFE 50 % / D LIFE 80 %), steady + fast repeats
- [ ] Listening verdict (lab tab "Repetition") — exit condition: engine preferred over identical repeats

## Milestone checklist — Phase 5: dynamic synthesis

- [x] velocity -> intensity (velocity 100 = as recorded; soft range wide, hard range narrow)
- [x] velocity -> transient, spectral tilt (high shelf), attack bite (decaying shelf), body, pitch transient, damping, soft attack
- [x] DYNAMICS macro (0 = velocity is volume only; 0.5 calibrated; 1 = twice)
- [x] Experiment 4 (A gain / B gain + filter / C model 50 % / D model 100 %), 5 sources
- [ ] Listening verdict (lab tab "Dynamics") — exit condition: crescendo clearly more than gain
- Known limit: near-sinusoidal sources (some vocals, soft plucks) have no upper harmonics to
  brighten; velocity then acts mainly on the attack. Harmonic generation belongs to Reimagined.

## Milestone checklist — Phase 6: Original <-> Reimagined and the remaining macros

- [x] Multiple pitch branches (Tape/Natural) + continuation depth (segment length) by Reimagined
- [x] Spectral-envelope transformation: CHARACTER moves the source's own body resonances + tilt
- [x] Resonance reconstruction: sympathetic resonator bank (source partials + body peaks)
- [x] Harmonic manipulation: soft saturation towards the Reimagined end
- [x] MOTION: drift depth/rate, stereo motion, jump rate; nearly inactive on plucks
- [x] SPACE: width, decorrelation for narrow sources, FDN ambience, loudness trim
- [x] Macro smoothing (no clicks when automating)
- [x] Experiment 5 (Reimagined 0/25/50/75/100 %, 4 sources)
- [ ] Listening verdict (lab tab "Original ↔ Reimagined") — exit: useful sounds across the range
- [ ] Transient/body separation (spec §19) — not yet; transient shaping is gain-based

## DONE

- Pure C++20 DSP/analysis library (`osp_dsp`) independent of JUCE; JUCE used for file
  formats, MIDI files, JSON, SHA-256.
- Deterministic rendering: bit-identical across runs and across block sizes
  (32–1024); seed-dependent randomisation in baseline B.
- 65 Catch2 test cases (unit/integration/regression) + performance smoke + 5 plugin
  test cases; all green on Linux (GCC 13) and macOS 14 arm64 (Apple Clang 15, CI).
- Phase 1 plugin: see checklist above.
- Synthetic ground-truth generators and a synthetic mini-corpus generator
  (`--generate-test-signals`), including deliberately broken/unsupported files.

## IN PROGRESS

- Phase 2 pitch bake-off: waiting on blind listening ratings for run 1.

## NEXT

- Score run 1 and record the engine decision; wire the winner (if not A) as an
  offline-prepared option behind `ModelExchange`, with A/B renders against baselines A and B.

## BLOCKERS

- DAW testing (Logic, Ableton, Reaper) needs a Mac; CI covers build, tests and auval.

## KNOWN ISSUES

- **CPU (engine C)**: about 1.2x the baseline per voice (continuation crossfades, shelves,
  post stage). Measured on the cloud VM this session: baseline 16 sustained voices 31-33 %
  (the same VM measured 20 % earlier: noisy host), engine C 38-40 %. CI now benchmarks
  engine C on macOS arm64; the spec target is 16 voices < 25 % there.
- **CPU**: the windowed-sinc kernel is computed per output sample (two table lookups
  per tap). Measured on a 2.1 GHz Xeon cloud VM: 16 sustained voices ≈ 19–20 % of the
  48 kHz/128 block budget, 24 voices with dense retriggers ≈ 33 %. Transposing up
  stretches the kernel (more taps). Planned fix when it matters: mip-mapped
  (pre-decimated) sources prepared off-thread so the kernel never stretches, and/or
  a precomputed polyphase table. Not done in Phase 0 to keep the baseline simple.
- Worst-case callback times on shared cloud VMs show scheduler spikes (several ms);
  they need measuring on the real Apple Silicon target.
- YIN range is 30 Hz – 4 kHz. Notes outside it are not judged by render pitch checks;
  very low basses (< 30 Hz fundamentals) will report a wrong/low-confidence root.
- Onset list uses spectral flux with a 3 dB/bin absolute floor: very soft attacks
  (bowed swells) may produce no listed onset (the envelope `onsetSeconds` still exists).
- Pitch analysis runs at the source rate; 96 kHz sources cost ~2x analysis time.
- Baseline voices stop when the recording ends (no looping) — by design until the
  Continuation Engine; `long-hold`/`long-chord` renders of short sources are mostly
  silence after the source ends.
- JUCE writes integer AIFF only; float AIFF-C is read (tested) but never written.
- Cross-platform bit-identity is not guaranteed (libm); golden tests use tolerances.
- Plugin: no undo/redo of sample loads yet (parameters have an UndoManager); multi-file
  drops use the first file; mod wheel / channel pressure have no destination yet; no
  MPE; the UI is a debug UI (no scaling options beyond window resize).
- Plugin RT caveat: merging on-screen keyboard events uses JUCE's
  `MidiKeyboardState::processNextMidiBuffer`, which takes a short internal lock and can
  grow the MIDI buffer when UI notes are injected. Standard JUCE practice, but it bends
  Rule 1; replace with a lock-free FIFO from the UI when the real UI is built.
- Bake-off engine C (formant compensation) goes an octave or more off pitch at ±24
  on organ and bowed sources; B (same library, plain) is clean there.
- The cepstral envelope-shift metric is only reliable on synthetic sources; on real
  high-F0 or broadband sources it is noise. Guard rail only.
- Plugin: a host that never calls processBlock keeps the previous instrument alive in
  memory until processing starts (by design of the hand-over; harmless).
