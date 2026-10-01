# Status

_Last updated: Phase 0 complete (pending review with the real corpus)._

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
- [ ] Run the real 80-file corpus (requires the files locally — see Blockers)
- [x] Optional minimal standalone/plugin (see Phase 1 below)

## DONE

- Pure C++20 DSP/analysis library (`osp_dsp`) independent of JUCE; JUCE used for file
  formats, MIDI files, JSON, SHA-256.
- Deterministic rendering: bit-identical across runs and across block sizes
  (32–1024); seed-dependent randomisation in baseline B.
- 64 Catch2 test cases (unit/integration/regression) + performance smoke; all green.
- Synthetic ground-truth generators and a synthetic mini-corpus generator
  (`--generate-test-signals`), including deliberately broken/unsupported files.

## IN PROGRESS

- Phase 1 (playable JUCE sampler) — see below.

## NEXT

- Review Phase 0 against the real corpus (`--corpus research/corpus --profile standard --engines A,B`).
- Phase 2 pitch bake-off (A resampling / B Signalsmith Stretch / C formant-aware), see the Phase 0 report.

## BLOCKERS

- The real research corpus is not in the repository (by design: git-ignored). Corpus
  success counts and real-source pitch observations need a local run.

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
