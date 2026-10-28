# 02 — ANDOR Grain vs OSP's Granulator

Prototype source: `origin/andor-lab` at `54afd07` (forked from `50103c0`, i.e. before CPU
step 2), extracted read-only with `git archive`; paths below are in that tree. The lab's own
engine tests were built and run in this session: **43 test cases, 6 026 assertions, all pass**
(`tests/andor_tests`). Its plugin tests were not built here.

## 1. What Grain is

- `osp::grain::GrainEngine` (`src/andor/grain/GrainEngine.h:160-283`): a self-contained
  polyphonic engine with its own voices (16 + 4 steal slots), its own ADSRs, LFOs, SVF filter,
  wavetable oscillator, its own 8-slot modulation matrix and its own effects wrapper.
- `GrainModel`: one shared padded `PlaybackSource` + whole-file region + optional atlas
  regions and `keyRegions[128]`, immutable, published through `ModelExchange`.
- **Global grain pool** of 512 with a quality budget (Eco 96 / Standard 192 / High 384) and a
  deterministic **fair share** (`budget / soundingVoices`, max 160 per voice), denials counted
  (`GrainEngine.cpp:515-523`).
- Scheduler per voice, `sr/density · (1 ± 0.9·jitter)`, jitter a user parameter.
- Per grain at spawn: length, pitch (`pitchSemitones + pitchScatter·U(-1,1)`), centre
  (position + scatter, mapped inside the region), reverse with probability, equal-power pan
  from SPREAD, gain `1/√(density·length)`; rate frozen for the grain's life (`:509-561`).
- Windows: Hann, Tukey (Soft), Percussive tables with linear interpolation (`:135-147`).
- No SIMD; scalar Hermite per channel.

## 2. KEY DNA (the genuinely new idea)

`src/andor/grain/KeyDna.h/.cpp`: 7 parameters (size, density, position, positionScatter,
pitchScatter, tone, spread). For each:

1. a drawn curve over the keys (≤ 16 points, linear, −1..1);
2. a **stable per-key individuality** from a seed: smooth knots every 12 keys
   (`deriveSeed(seed, p+1, j)`, cosine-interpolated) blended with per-key white noise
   (`deriveSeed(seed, 1000+p, key)`) by INDIVIDUALITY, renormalised to keep the spread;
3. `offset = curveAmount·curve + variationAmount·variation`, cached as a 128×7 table,
   recomputed allocation-free when settings change.

Evaluation order (`GrainEngine.cpp:474-489`), in the normalised knob domain:
**base → +½·DNA → +½·per-trigger variation → +Σ modulation → clamp → convert → 20 ms
smoothing → DSP.** The DNA offset is snapshotted at note-on (held notes keep their identity
while the curve is edited); per-trigger variation comes from the event seed; per-grain
randomness lives in `spawn`. Tests prove the three are separate (`GrainTests.cpp:438-474`) and
that INDIVIDUALITY moves the neighbour correlation monotonically from > 0.9 to < 0.25.

## 3. Code-proven differences

| Aspect | ANDOR Grain | OSP GranularSource (today) |
|---|---|---|
| Polyphony model | per voice, 16 voices | per voice, 24 per layer × 3 layers — **same model** |
| Grain ownership | global pool 512, fair-share budget | 24 per stream, 2 streams per voice, **no budget** |
| Per-key identity | **KEY DNA** (curve + seeded individuality, 7 params) | none (LIFE varies per *trigger*) |
| Per-trigger variation | `triggerVariation` ±0.25 normalised | LIFE: gaussian offsets on POS/SIZE/DENS/SPREAD/TUNE, LOOSE/FRAY |
| Pitch | per-grain scatter 0–12 st, rate frozen per grain (bend steps grain to grain) | TUNE only, but **live per-sample step** (bend, glide, vibrato follow every grain) |
| Stereo | per-grain equal-power pan | none (L/R at one position) |
| Reverse | per-grain probability | layer switch (all grains) |
| Windows | Hann / Tukey / Percussive | Hann (rotation) |
| Ranges | size 5 ms–1 s, density 1–200/s | size 20–400 ms, density 4–40/s (LIFE/mod up to 10–600 ms, 2–60/s) |
| Position | bounded to a region (atlas note or whole file) | whole recording |
| Modulation | **second matrix**: 8 slots, own sources/destinations, per-voice LFOs restart at note-on | OSP's one matrix (12 sources, A/B/C destinations, meta-mod, global/poly LFOs, sync) |
| Oscillator | band-limited wavetable blend (4 shapes × 11 octaves) | none |
| Filter | per-voice SVF | CHARACTER per voice (ladder/SVF/TILT) + DAMPEN |
| FOLLOW, REIMAGINED doubling, KALEIDOSCOPE, mode engines | absent | present, integrated with grains |
| SIMD | none | Hermite tap weights in four lanes |
| Determinism | seeded; **gap**: S&H LFO order counter not reset on transport restart | seeded per note |

Measured on the same VM in this session (48 kHz, 256-sample blocks; `bench/`):

| Case | Load | Grains |
|---|---|---|
| Grain "typical" preset, 8 voices (oscillator 25 %, SPACE, ECHO, LFO → POS) | 5.2 % | 24 |
| OSP 1 layer Granular, 8 notes (default macros) | 4.0–5.4 % | 21 |
| Grain "stress" (600 ms, 120/s, scatter), 16 voices | 18.7 % | 225 of 384 |
| OSP 3 layers Granular dense (300 ms, 30/s), 8 notes | 19.7–21.4 % | 236 |

**Per voice the two cost about the same.** Grain's advantage is not speed but that its budget
bounds the worst case; OSP's advantage is SIMD reads and live pitch.

## 4. Duplicated functionality

The Grain voice shell (ADSR, steal, LFO, filter, modulation slots, effects) duplicates OSP's
`InstrumentVoice` / `Modulation` / `PostProcessor` machinery; `SharedFx` is OSP's
`SpaceReverb` + `EchoDelay` at fixed settings; `andor::Lfo`, `Svf`, `Smoother` and the scalar
Hermite duplicate existing OSP pieces. **Only KEY DNA, the budget, the per-grain options,
region-bounded placement and the wavetable bank are new.**

## 5. Reuse verdicts

| Component | Verdict | Why |
|---|---|---|
| `KeyDna.h/.cpp` (`DnaCurve`, `KeyDnaTable`) | **Reuse almost unchanged** — map its parameter enum onto OSP's granular parameters, document in `analysis-schema.md`, version the persisted form | pure C++, allocation-free, seeded, tested; fills OSP's missing per-key layer |
| Global budget + fair share | **Adapt** into the engine (one counter across A/B/C consulted by `GranularSource::spawn`) | bounds OSP's unbounded worst case; must not deny grains in existing presets (see 05) |
| Per-grain pitch scatter, pan, reverse probability, window tables | **Adapt** as options in `GranularSource`, neutral defaults, own PRNG stream | small, valuable; keep OSP's per-sample step |
| Region-bounded placement | **Adapt** only through the atlas source (04/06) | — |
| `GrainEngine` voice/scheduler/envelope shell | **Do not transplant** | duplicates `InstrumentVoice`, freezes pitch per grain |
| 8-slot matrix, per-voice LFOs | **Drop** in favour of `Modulation.h` | a second matrix breaks Rule 6 |
| `Wavetable` bank + oscillator | **Defer** (reusable later as is; move per-sample cos/sin to control rate) | not needed for the unified source; see brief §9 |
| `SharedFx`, `DspUtil` (`Svf`, `Lfo`, `Smoother`, scalar Hermite) | **Do not reuse** | OSP has the originals or better |
| `KeyDnaEditor` (GUI) | **Adapt as interaction reference** (curve + individuality bars + live dots), in OSP components | proven interaction; prototype styling |
| `--andor-bench`, allocation-counting and region-marker tests | **Adapt** into OSP's research harness and tests | good method |
