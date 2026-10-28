# 09 — Performance and risks

## 1. Method and hardware

- **Hardware:** virtualised Linux container, 4 vCPU "Intel Xeon Processor @ 2.10GHz", GCC 13.3
  `-O3`, Release. **Not a Mac**: absolute numbers do not transfer to Apple Silicon (earlier
  measurements put a Mac at roughly 2–4× this VM, unverified for these scenes). Single-block
  "worst" values include virtualisation jitter.
- **OSP:** `bench/bench_osp.cpp`, a scratch harness outside the build that links
  `build/libosp_dsp.a` and drives `InstrumentEngine` like the lab's harness: N notes held, one
  re-triggered every 0.5 s, 6 s per scene, 48 kHz, default macros (REIMAGINED 0.2, SPACE 0.2,
  LIFE 0.5, CHARACTER ladder), synthetic stereo sources (3 s pluck, 6 s tone, 3 s harp; a
  12-member set of 1.2 s notes). Grains = the display snapshot (≤ 128 per layer).
- **Grain / Phrase:** the lab's own `research-renderer --andor-bench` (typical and stress
  presets), built from `origin/andor-lab` in the scratchpad, same machine, same day.
- **Memory:** not measured here for OSP scenes; Phrase figures are the lab's (`benchmarks.md`).

## 2. OSP engine C today (48 kHz, 256-sample blocks; `bench/osp-256.txt`)

| Scene | Load (mean) | p99 | Voices (peak) | Grains (peak) |
|---|---|---|---|---|
| 1 layer one-shot, 1 note | 2.0 % | 3.2 % | 2 | — |
| 1 layer one-shot, 8 notes | 4.7 % | 7.5 % | 10 | — |
| 1 layer one-shot, 16 notes | 7.0 % | 10.9 % | 18 | — |
| 1 layer one-shot, 8 notes, REIMAGINED 0 / 100 | 4.7 / 8.5 % | 14.0 / 11.6 % | 10 | — |
| 3 layers one-shot, 8 notes | 13.8 % | 23.8 % | 30 | — |
| 1 layer granular, 8 / 16 notes | 4.0 / 9.6 % | 6.4 / 18.4 % | 9 / 17 | 21 / 44 |
| 1 layer granular dense (300 ms, 30/s), 8 notes | 6.3 % | 10.4 % | 9 | 79 |
| 3 layers granular, 8 notes | 12.3 % | 19.5 % | 27 | 60 |
| 3 layers granular dense, 8 notes | 19.7 % | 31.7 % | 27 | 236 |
| A one-shot + B, C granular, 8 notes | 14.1 % | 22.9 % | 28 | 41 |
| 3 layers granular, REIMAGINED 100, 8 notes | 23.4 % | 36.8 % | 27 | 86 |
| 1 layer 12-member set ("atlas-like"), 8 / 16 notes | 2.5 / 4.7 % | 4.7 / 16.5 % | 9 / 17 | — |
| set + 2 granular layers, 8 notes | 11.6 % | 19.7 % | 27 | 41 |
| 3 layers one-shot, 8 notes, pedal | 20.1 % | 36.8 % | 38 | — |
| 3 layers granular, 8 notes, 16 routes (max modulation) | 20.3 % | 43.4 % | 28 | 248 |
| 3 layers granular, 16 notes, 16 routes | **39.8 %** | **81.0 %** | 51 | 384 |

At 128-sample blocks the heaviest scene is 40.6 % mean, 73.6 % p99, 11 overruns in 2 240
callbacks (`bench/osp-128.txt`).

## 3. Grain and Phrase prototypes (same VM; `bench/lab-typical.txt`, `lab-stress.txt`)

48 kHz, 256-sample blocks, mean load for 1 / 4 / 8 / 12 / 16 voices:

| Engine | 1 | 4 | 8 | 12 | 16 | Grains at 16 |
|---|---|---|---|---|---|---|
| Grain typical | 3.2 % | 3.5 % | 5.2 % | 6.9 % | 8.0 % | 35 / 384 |
| Grain stress | 6.1 % | 11.2 % | 13.8 % | 19.4 % | 18.7 % | 225 / 384 |
| Phrase typical | 2.1 % | 3.1 % | 4.5 % | 8.2 % | 7.5 % | — |

## 4. What this says

- **Per voice, OSP's granulator and Grain cost the same** (≈ 0.5 % per 8-note cloud at
  default density on this VM). Integrating KEY DNA into OSP adds a table read per note —
  negligible.
- **The unbounded case is OSP's**: heavy modulation on three granular layers with 16 notes
  reached 384 grains and 40 % mean / 81 % p99 here. Grain's budget is the piece worth porting.
- **Atlas sets are cheap**: short regions end, so a 12-member set costs less than one long
  one-shot.
- **Hybrid Natural+Grain** is not measurable today; estimate = one-shot + granular per layer
  (≈ 9–10 % per 8-note layer here, ≈ 30 % for three) — **must be benchmarked** in its phase.
- **Three advanced layers**: the realistic ceiling on this VM at 256 samples is ~40 % mean
  with p99 near 80 %; on a Mac this should be comfortable, but needs measuring there
  (`bench_osp` builds anywhere `osp_dsp` does).

## 5. Memory

- One-shot: the decoded file + a padded copy (as today).
- Atlas with region copies: ≈ the phrase length ×2 (copy + padding) — 30 s stereo 48 kHz
  ≈ 23 MB per layer (lab figure: 2 × 11.5 MB).
- Voices: `InstrumentVoice` already inlines 2 GranularSources and 4 mode engines (~1.5 MB for
  208 slots); KEY DNA adds a few floats per voice and 128 × 7 floats per layer (3.5 KB).

## 6. Risk register

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | A new option shifts an existing PRNG stream → every granular preset changes | medium | high | new streams with new tags only; reference-render tests bit-identical |
| R2 | Budget denies grains in an existing heavy preset | low–medium | medium | default unlimited (or above the structural max); opt-in budget |
| R3 | State 13 migration bug loses sessions | low | high | missing params default to neutral; round-trip tests for 12 → 13; keep decoders for c1–c3 |
| R4 | Phrase analysis fails on real recordings (only synthetic evaluation exists) | **high** | medium | evaluate on real phrases before committing the GUI; "Play as one sound" stays the default |
| R5 | Region copies cost memory/time on long phrases | medium | low | shared-buffer regions later (Architecture B core) |
| R6 | NOTE MAP is cramped in triple density | high | low | zoom-to-note; band takeover option |
| R7 | Hybrid doubles per-voice cost | high | medium | skip paths at 0 and 1; budget; measure |
| R8 | KEY DNA without the editor is invisible to users | medium | low | ship the editor with the feature, or keep it behind presets until it exists |
| R9 | Two prototype stores/IDs collide with OSP's | low | low | do not port `AndorStore`; use the Library store |
| R10 | Choice-parameter growth shifts normalised automation | medium | medium | never append choices to existing choice params (e.g. `sourceMode`); add new params |
