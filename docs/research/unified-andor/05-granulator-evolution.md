# 05 — Evolving the Granulator (static, KEY DNA, hybrid)

## 1. The three approaches from the brief

| | A. Extend `GranularSource` | B. Replace its DSP with Grain's behind an adapter | C. Shared core, simple/advanced policies |
|---|---|---|---|
| Code complexity | low: additive options + one hook | high: adapter for scheduler, windows, norm, pitch, FOLLOW, doubling, mode engines | medium-high: refactor both into one core |
| DSP correctness | keeps per-sample live pitch, FOLLOW, doubling | loses live pitch (rate frozen per grain), FOLLOW, doubling unless re-added | depends on the merge |
| Legacy audio | **bit-identical by construction** when options are neutral | **not identical** (different scheduler RNG use, windows, gain law) | identical only through a legacy path |
| Real-time safety | unchanged (fixed pools) | both are RT-safe | — |
| CPU | today's (SIMD) + small per-grain extras when used | Grain's scalar reads (no SIMD) | — |
| Polyphony | unchanged (per voice) | unchanged | unchanged |
| Parameters | existing IDs kept, new ones appended | existing IDs remapped | existing kept |
| Testing burden | "neutral = identical" on every reference render + new feature tests | full re-validation + listening tests of every granular preset | both |
| Maintainability | one engine | one engine, but OSP features re-implemented | one engine after a large merge |
| Effort | days per option | weeks | weeks |

**Recommendation: A.** Grain's *engine* is not better than OSP's granulator; its *features* are.
Port those features (KEY DNA, budget, per-grain options) into OSP's `GranularSource`.

## 2. One engine, two behaviours — without a second switch

There is no need for a "Simple / Advanced granular" switch. Every new capability is an
amount whose neutral value is today's behaviour:

| Capability | Parameter (new, appended) | Neutral = legacy |
|---|---|---|
| KEY DNA | `layerX.dna.amount` (curve), `layerX.dna.variation`, `layerX.dna.individual`, seed | 0, 0 → no offsets |
| Pitch scatter | `layerX.granular.pitchScatter` (st) | 0 |
| Grain pan | `layerX.granular.pan` (0..1) | 0 |
| Reverse probability | `layerX.granular.reverseChance` | 0 (layer REVERSE keeps its meaning) |
| Window | `layerX.granular.window` (Hann rotation / Soft / Percussive) | Hann rotation (today's code path, not a table) |
| Wider SIZE/DENS ranges | **not** by changing the existing parameters' ranges (that would move normalised automation); via KEY DNA/modulation reach or new "range" option | existing ranges |

So "static" behaviour is simply KEY DNA at 0; "advanced" is the same voices with a per-key
profile. Both are the same DSP path.

## 3. Boundaries of the legacy guarantee

The guarantee "an existing preset sounds the same" holds if:

1. **No change to existing PRNG streams.** New per-grain randomness (pitch scatter, pan,
   reverse) draws from a **new stream** `deriveSeed(shape.seed, <new tag>, 0)`, never from the
   grain-position stream (`0x6772736d`). Drawing from the old stream, even conditionally,
   shifts every later grain.
2. **Neutral offsets are exact identities**: `× 1.0f`, `+ 0.0f` and unchanged clamp order in
   `notesGranular()` produce bit-identical values (IEEE), so KEY DNA can sit in that function.
3. **The budget never denies in legacy scenes.** Default budget = unlimited (or a ceiling
   above the structural maximum of 3 × 24 × 2 × 24 = 3 456 grains); a finite budget is opt-in
   (e.g. tied to the existing CPU/quality idea, or set when advanced ranges are used). Decision
   for the user — see the brief.
4. **Windows**: keep the rotation Hann as the default code path; tables only for the new
   shapes.

Verification: every `tests/audio/reimagined-before/*granular*` reference render, the
ModulationTests "no route is exactly no modulation" and determinism tests must stay
bit-identical; new tests add each option at non-neutral values.

## 4. Static vs KEY DNA, concretely

Per voice at note-on (today): `NoteShape` gets LIFE's per-trigger offsets (`granularLife`,
`InstrumentEngine.cpp:231-256`). Proposed: the voice also gets the layer's KEY DNA offsets for
its key from a cached `KeyDnaTable` (one table read). `notesGranular()` composes:

```text
base (layer knob) → × / + KEY DNA (per key, stable)        ← new
                  → + modulation (routes, per voice)        (existing, clamped to knob ends)
                  → + KALEIDOSCOPE scatter                   (existing)
                  → × / + LIFE (per trigger)                 (existing)
                  → clamp
spawn():          → per-grain randomness (position spread, + new scatter/pan/reverse)
```

This matches Grain's precedence (base → DNA → per-trigger → modulation → per-grain) except
that OSP applies LIFE after modulation; since both are offsets/ratios before the final clamp,
the order differs only in where the intermediate "knob ends" clamp sits. Keep OSP's order so
existing presets do not move.

## 5. Natural ↔ Grain on one source (brief §7)

**Recommended model: GRAIN MIX inside One Shot**, not a third source mode.

- `sourceMode` keeps its two choices (adding a choice would change normalised automation).
- New `layerX.grainMix` 0..1, meaningful in One Shot: 0 = today exactly (no `GranularSource`
  stepping at all); 1 in One Shot = grains replace the read (equivalent to Granular but with
  the One Shot attack kept, see below); Granular mode stays as is.
- **Cost control**: at 0 the granular path is skipped (no spawn, no reads); when the mix has
  been 1 for a whole control period the sinc read and continuation can be bypassed too (with a
  15 ms fade when the amount moves off 1 again).
- **Precedent**: KALEIDOSCOPE already mixes an internal grain pool over the dry read with a
  delayed onset and a crossfade (`InstrumentVoice.cpp:1140-1186`); the same structure,
  driven by a user amount instead of REIMAGINED, gives Natural ↔ Grain.
- **Pitch / latency**: both paths run in the same voice at the same `currentStep`, no latency
  difference, no double trigger (one voice lifecycle, one envelope, one steal).
- **Phase**: the cloud reads other positions than the read head (POS/spread), so it is mostly
  uncorrelated with the dry signal → **equal-power** crossfade keeps loudness; where grains
  sit close to the read head (POS following the head, small spread) the signals correlate and
  equal-power bumps up to +3 dB — mitigate with a correlation-aware gain like the continuation
  crossfade already uses (`crossfadeGains`, `InstrumentVoice.cpp:1091-1100`).
- **Transients**: keep the natural attack, bring the cloud in after an onset delay (KALEIDOSCOPE
  uses 120 ms) — musically the most useful hybrid ("real attack, granular sustain").
- **CPU** (measured parts): one layer one-shot 8 notes ≈ 4–5 %, granular 8 notes ≈ 4–5.5 % on
  this VM → hybrid ≈ 9–10 % per layer at 8 notes, ≈ 30 % for three hybrid layers (estimate
  from parts, not measured; must be benchmarked in Phase 6).

Alternative considered: a time-based "BLOOM" (natural attack morphing into grains over N
seconds) — same machinery, an envelope on GRAIN MIX; can be offered as a modulation route
(ENV → grainMix) instead of a new control, so no extra UI.

## 6. Global grain budget (brief §8)

- One engine-level budget across A/B/C, fair share = budget / sounding voices (Grain's rule),
  deterministic, consulted in `GranularSource::spawn`.
- Gain normalisation should use the **admitted** density, not the requested one (both
  prototypes get quieter when grains are denied — a known flaw to avoid).
- Measured need: the heaviest scene here (3 layers granular, 16 notes, 16 routes) reached 384
  displayed grains at 40 % load on this VM; a budget of ~256–384 for live playing, unlimited for
  offline bounce, would cap that.
