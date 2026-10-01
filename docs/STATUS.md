# Status

_Last updated: Phase 0 complete; Phase 1 (playable sampler plugin) implemented. Both pending review with the real corpus and on macOS hosts._

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
- [ ] Exit check with real corpus samples in a DAW

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

- Nothing. Waiting for review of Phase 0/1 before the Phase 2 pitch bake-off.

## NEXT

- Decide on the corpus-run findings: start-at-onset and playback-gain options, `Sus`
  filename/pitch offsets, root choice for harmonics (`docs/reports/corpus-run-1.md`).
- Phase 2 pitch bake-off (A resampling / B Signalsmith Stretch / C formant-aware), see the Phase 0 report.

## BLOCKERS

- DAW testing (Logic, Ableton, Reaper) needs a Mac; CI covers build, tests and auval.

## KNOWN ISSUES

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
- Plugin: a host that never calls processBlock keeps the previous instrument alive in
  memory until processing starts (by design of the hand-over; harmless).
