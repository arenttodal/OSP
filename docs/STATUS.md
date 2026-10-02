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
- [x] Transient/body separation (spec §19): offline HPSS of the onset region
  (`analysis/transient`); from about a fifth away the separated transient plays at its own
  speed, aligned on its peak, while its transposed copy is removed from the body.
  Synthetic picked tone two octaves down: pick 16 ms -> 4.5 ms (original 4.5 ms).
  On by default in engine C (`"transientPreservation"` in render configs).
- [x] Experiment 7 (`transients-1.json`: pluck, Tagel, nyckelharpa, violin at ±12/±24, off vs on)
- [ ] Listening verdict (lab tab "Transients"). Most corpus plucks carry little broadband
  attack (transient share 0.3–2 %, one pluck 15 %), so expect subtle differences.

## Milestone checklist — Phase 7: multi-sample intelligence

- [x] Group import (multi-file drop, folder drop, multi-select chooser; sessions store every member)
- [x] Pitch clustering (roots within half a semitone)
- [x] Round-robin inference + engine rotation that never repeats the previous take
- [x] Velocity inference (dynamics words in names, else loudness gaps >= 4.5 dB); one shared set gain keeps natural level differences
- [x] Alternate articulations (much shorter/longer takes) kept but not used for ordinary notes
- [x] Register model (brightness vs pitch, shrunk when few pitches); ground truth: plucks 6.2 -> 4.6 st error, organ 5.2 -> 4.8, tagel 3.0 -> 3.1 (neutral)
- [x] Samples inspector (role, layer, root corrections; rebuild without re-analysis; recall)
- [x] Experiment 6 (single file plain / single file engine / whole set)
- [ ] Listening verdict (lab tab "Multi-sample") — exit: a small related set is automatically better
- Known limits: sets mixing different organ stops are grouped by pitch only (timbre clustering
  is not done); register anchors are not built for sets.

## Milestone checklist — Phase 8: commercial polish (what can be done without a Mac/certificates)

- [x] Starting states (Natural, Alive, Floating, Broken, Frozen, Dream, Wide) as host programs + selector
- [x] Presets (.osppreset) and portable instruments (.ospinstrument: sources + analysis + settings; bit-identical on another machine)
- [x] Undo/redo: sample loads (incl. sets and stages), root changes; parameters via the APVTS undo manager
- [x] UI scaling 80–200 %, accessibility titles on controls
- [x] MIDI: mod wheel, aftertouch/pressure, CC74, CC 20–25 -> macros; MPE lower zone (bend/pressure/slide per note)
- [x] Windows VST3: CI builds it and runs every test with MSVC
- [x] Performance tuning: polyphase tables for unity and stretched reads (semitone grid up to x4); engine C costs about two-thirds of the plain baseline
- [x] Host robustness matrix (plugin test): 22.05–192 kHz × blocks 1–4096 and host-varied blocks; bit-identical output, finite, click-free, chord ±2 octaves + bend
- [x] Crash-safe sample management (atomic store copies, analysis cache re-derived when stale, original-path fallback)
- [x] User guide (docs/user-guide.md)
- [~] Installer/signing/notarisation: `scripts/package-macos.sh` written, **not run** (needs a Developer ID)
- [ ] Factory example sounds (need legally owned recordings — see morning checklist)
- [ ] Preset browser beyond file load/save (a list of the user's presets)
- [ ] Host certification matrix (Logic, Ableton, Reaper, Cubase, Studio One) — needs a Mac
- [ ] Onboarding beyond the empty-state prompt and "Load example"

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

- Listening verdicts for Phases 3–7 (one page: the listening lab, see docs/morning-checklist.md).
- DAW/host validation on a Mac.

## NEXT

- Score the lab ratings; tune or revert per experiment (keep the simplest winner).
- Score lab tab "Transients"; if transient preservation loses or ties, switch it off by
  default (simplest wins). Spectral/stochastic continuation only if multi-loop loses.
- Real-Mac worst-case callback timing. Reads above x4 (more than about +2 octaves) still
  evaluate the stretched kernel; mip-mapped sources would cap them if that matters.

## BLOCKERS

- DAW testing (Logic, Ableton, Reaper) needs a Mac; CI covers build, tests and auval.

## KNOWN ISSUES

- **CPU (engine C)**: about 1.2x the baseline per voice (continuation crossfades, shelves,
  post stage). macOS 14 arm64 CI runner (48 kHz / 128): engine C 16 held voices **17.1 %**
  mean (spec target < 25 %: met), 24 voices with dense retriggers 30.5 % (baseline A 26 %).
  Worst-case callbacks on the shared CI VM spike to several ms; needs measuring on a real Mac.
  Since then engine C reads at or below the original speed through a precomputed
  polyphase table (2048 phases): on the cloud VM 16 held voices went 38 % -> 22 % (the
  baseline measures 31 % on the same VM), 24 dense voices 63 % -> 41 %. Stretched reads
  (upward transposition) then got polyphase tables too (24 semitone levels up to x4,
  next level up so the cutoff is never higher; 4.5 MB per engine): 16 held voices
  22 % -> 10 %, an octave up 38 % -> 17 %, 24 dense voices 41 % -> 21 % (baseline A
  32 % on the same VM).
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
- Very short soft sources (< 0.5 s synthetic vowel) can be pitch-tracked on a harmonic
  with moderate confidence (0.4 s vowel at C4 -> G5). Longer material is unaffected.
- The synthetic vowel at A4 (strong 2nd-harmonic formant) is tracked an octave up with
  confidence 0.46 ("moderate"). Real corpus files were fine (66/80 high confidence), but
  multi-sample sets inherit any octave error into their pitch grouping; the Samples
  inspector shows each file's group so it can be corrected.
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
