# Phase 1 — KEY DNA inside OSP's existing Granulator (engine only)

Each key can now give a Granular layer its own stable identity, inside OSP's own granulator.
It is optional and off by default. Nothing in the plugin, its GUI, its parameters or its state
changed. Off, OSP renders bit-identically to the Phase 0 baseline.

## 1. What was built

| File | What |
|---|---|
| `src/engine/KeyDna.{h,cpp}` | `keydna::Settings` (per layer), `keydna::Table` (the 128-key profile), `keydna::apply()`. The mathematics are ANDOR Lab's `KeyDna` (`andor-lab` at `54afd07`, `src/andor/grain/KeyDna.*`), on OSP's own five parameters and OSP's own curve tables. |
| `src/engine/NoteShape.h` | `KeyDnaOffsets`: one key's offsets, carried by the note (`NoteShape::keyDna`) |
| `src/engine/InstrumentEngine.{h,cpp}` | `setLayerKeyDna(layer, settings)`: recomputes that layer's table only when its settings change. At note-on, a Granular voice takes its key's offsets. |
| `src/engine/InstrumentVoice.cpp` | `notesGranular()` applies the offsets first, to the layer's own settings |
| `src/research/RenderConfig.*`, `RenderSession.*`, `UnifiedScenes.*` | per-layer `"keyDna"` in render configs; the demonstrations; per-key measurements; `--unified --set keydna`; `--with-keydna` |
| `research/configs/keydna-{a-static,b-organic,c-expressive}.json` | the three demonstration patches, for any recording (`--source`) |
| `tests/unit/KeyDnaTests.cpp`, `tests/integration/KeyDnaEngineTests.cpp`, `tests/regression/UnifiedBaselineTests.cpp` (demonstrations), `tests/support/AllocationCounter.*` | see §5 |

One shared granular implementation: `GranularSource` is unchanged. There is no second engine,
no second modulation matrix, no new voice-allocation path.

## 2. The model

Per layer (`keydna::Settings`):

- **enabled**, **amount** 0..1 (scales the whole profile), **individuality** 0..1 (Smooth ↔ Individual), **seed**;
- per parameter POS, SIZE, DENS, SPREAD, TUNE:
  - **curve**: an OSP curve table over the keys, where 0.5 means no offset;
  - **curve depth** −1..1;
  - **variation** 0..1 (the depth of the key's individuality).

Each key gets one offset per parameter, decided once:

```text
unit   = clamp (depth · (2·curve(key/127) − 1) + variation · individual(key), −1, 1) · amount
offset = unit · halfRange(parameter)
individual(key) = (a·smooth(key) + b·white(key)) / √(a² + b²),   a = 1 − individuality, b = individuality
```

- `smooth(key)` is a random knot every 12 keys, cosine-interpolated, so neighbouring keys are
  nearly equal.
- `white(key)` is an independent value per key.
- The blend is renormalised, so the spread of values is the same at every setting. Measured
  neighbour correlation falls monotonically from above 0.9 (Smooth) to near 0 (Individual)
  (unit test).

**Half ranges** are the furthest one key moves each parameter, in the parameter's own units.
They are half of each knob's travel, in the units the modulation system already uses for the
same knobs:

| Parameter | Unit | Half range |
|---|---|---|
| POS | fraction of the recording | ±0.5 |
| SIZE | octaves (20–400 ms) | ±2.16 |
| DENS | octaves (4–40 /s) | ±1.66 |
| SPREAD | 0..1 | ±0.5 |
| TUNE | semitones | ±12 |

**Ranges.** After the offset, each parameter is clamped to its existing knob: POS and SPREAD
0..1, SIZE 20–400 ms, DENS 4–40 /s, TUNE ±12 st. No parameter ID, range or automation meaning
changed. A parameter with a zero offset is not touched at all: no arithmetic and no clamp.

**Randomness (Rule 2).** The individuality comes from a new stream, root
`deriveSeed(seed, 0x6b6579646e61 "keydna", 0)`:
- the knots use `deriveSeed(root, 1 + p, j)`;
- the per-key values use `deriveSeed(root, 0x100 + p, key)`.

Computing a profile draws nothing from any existing stream (the list is in phase-0-baseline.md
§2). The profile depends only on its settings, never on what was played before. LIFE (per
trigger) and the grain scheduler (per grain) keep their own streams, untouched.

**Real-time.** Computing all 128 keys takes about 1 300 PRNG draws and allocates nothing (a
fixed `std::array`), so `setLayerKeyDna` is safe on the audio thread. It compares the settings
and returns early when they are unchanged.

## 3. Evaluation order (per Granular voice)

```text
1 the layer's knobs (POS + START, SIZE, DENS, SPREAD, TUNE; REVERSE, FOLLOW)
2 KEY DNA: this key's offsets, fixed at note-on        <- new; clamped to the knobs' ranges
3 modulation routes, per voice (VEL, NOTE, ENV 1/2, LFO 1/2 poly or global, MOD WHEEL, CC 1-4,
  PRESSURE, meta-mod depths): POS/SPREAD added, SIZE/DENS x 2^octaves, clamped to the knobs' ends
  (+ the modulated START offset and the REIMAGINED mode engines' grain offset)
4 KALEIDOSCOPE scatter (REIMAGINED on grains)
5 LIFE: this trigger's offsets and ratios
6 the final clamp
7 per grain at spawn: position spread and jitter, interval jitter (the scheduler's own stream)
```

This is the order proposed in 07-modulation-and-key-dna.md: base → DNA → modulation → LIFE →
grains. Steps 3–7 are exactly the existing code.

**Interactions:**
- **Velocity:** unchanged. The amplitude path does not see KEY DNA. VEL can still be routed to
  POS, SIZE, DENS or SPREAD, and it moves them around the key's own value.
- **NOTE:** the NOTE source is the live key through its curve, routed like any source. KEY DNA
  is a profile, not a source, so there is no second matrix. The two add: NOTE's route on top of
  the key's offset.
- **LFOs and envelopes:** poly or global as before, around the key's value.
- **Macros:** LIFE scales only stage 5. CHARACTER, DYNAMICS and the others do not touch grain
  parameters.
- **Existing granular modulation:** unchanged, step 3.

**Applied once.** KEY DNA is applied in one place, at the start of `notesGranular()`, which the
voice calls at note-on and at each control period. Each call starts again from the layer's
knobs, so the offset never accumulates. The integration test measures grains at base + offset
(not + 2 × offset).

**Pitch.** Note pitch tracking is the voice's per-sample step, unchanged. TUNE's offset adds
only to the grains' own TUNE ratio, so nothing is tracked twice.

**Mono legato.** A key that glides the sounding note keeps the first key's profile, because
the voice is not restarted. A new note takes its own.

## 4. Compatibility assessment

| Concern | Result |
|---|---|
| Existing sessions and presets | The plugin does not set KEY DNA yet, so every session plays as before. Proven by the plugin `[unified-baseline]` sessions and `[reimagined-migration]` (references unchanged). |
| Bit-identity, KEY DNA off | All 15 Phase 0 scenes have the same hashes (linux-x86_64-gcc). The plugin baseline and every older golden pass. |
| Bit-identity, KEY DNA on but neutral (on with nothing to move, AMOUNT 0, or switched off) | identical to off, on six scenes including modulation, layers and mono (`[keydna]`) |
| One Shot layers | ignore KEY DNA, bit-identical |
| Parameter IDs, ranges, automation | unchanged (no plugin parameter added; the schema golden passes) |
| Voice allocation | unchanged (the offsets ride on the note's shape) |
| Grain window | unchanged (Hann by rotation) |
| PRNG streams | unchanged (new stream only; the PRNG golden test untouched) |
| Other platforms | macOS arm64 compares level, peak, brightness and width (CI), not hashes. Apple Silicon fuses multiply-adds. |
| Block size | OSP's output already depends on it (Phase 0, finding 1). KEY DNA's own values do not: the identity test gives the same per-key positions at 64, 256 and 512 samples. |

## 5. Tests

| Test | Proves |
|---|---|
| `[unit][keydna]` (7 cases) | neutral settings give no profile; a neutral profile is a bit-exact identity, and untouched parameters stay untouched; the same settings give the same keys, another seed gives other keys, and C3, C4 and C5 differ; Smooth ↔ Individual is monotonic (> 0.9 down to < 0.3); curves map onto the keyboard, scaled by depth and AMOUNT, inverted by a negative depth; every offset is bounded and applied settings stay in the knobs' ranges (including out-of-range keys); `compute()` allocates nothing |
| `[integration][keydna]` (11 cases) | off, neutral or AMOUNT 0 is bit-identical to no KEY DNA (6 scenes); One Shot is untouched; DNA changes Granular output and is deterministic at 64, 256 and 1000 samples; **a key keeps its identity every time it is played** (grains measured at the key's own POS ±0.03, again within 0.02 on the next strike, at three block sizes; five keys spread > 0.15 of the recording); **polyphonic voices keep their own values** (two keys' grains form two clusters, < 5 % elsewhere); **A, B and C each have their own profile** (B's profile leaves A's and C's grains bit-identical); a sounding note keeps its profile when the settings change; a disabled route is exactly no modulation, a live one still moves POS; **no allocation** while profiles change and eight notes start and stop on three layers; extreme profiles stay finite and bounded on the stress scene; the research configs carry exactly the demonstration profiles |
| `[unified-baseline]` | the Phase 0 baseline is unchanged; the 15 demonstration renders are pinned (`tests/regression/golden/keydna`); A static equals the Granulator without KEY DNA bit for bit; B and C differ |

Full suites: see §8.

## 6. Measurements

### Per key (LIFE off, each key alone)

Full tables: `bench/phase1-keydna-keys-linux.md` (and `.json`). The expected grain position is
POS after the profile; the expected density of grains is DENS × SIZE.

| C expressive, key | POS (expected → measured) | SIZE ms | DENS /s | DENS × SIZE → grains at once |
|---|---|---|---|---|
| 36 | 0.465 → 0.495 | 203 | 12.2 | 2.48 → 2.22 |
| 43 | 0.253 → 0.261 | 123 | 10.1 | 1.24 → 1.20 |
| 48 | 0.615 → 0.609 | 239 | 12.3 | 2.95 → 2.79 |
| 60 | 0.670 → 0.665 | 109 | 18.3 | 1.99 → 1.92 |
| 72 | 0.100 → 0.091 | 77 | 14.0 | 1.08 → 1.02 |
| 84 | 0.720 → 0.717 | 79 | 23.2 | 1.83 → 1.67 |

- **A static:** every key the same, POS 0.45, measured 0.42–0.47.
- **B organic:** offsets within ±0.04 POS, ±0.23 octave SIZE, ±0.1 octave DENS; measured
  positions 0.44–0.49.
- **C expressive:** grains read from 0.09 to 0.72 of the recording depending on the key; long,
  sparse grains low and short, dense ones high, as drawn.
- **Level:** within ±1.5 dB across keys in C (the existing gain law; sparser keys are slightly
  quieter).

### Demonstration renders (LIFE 30 %, SPACE 15 %)

`bench/phase1-keydna-demos-linux.md`: 3 patches × singles, repeated, scale, chord4 and chord8,
with hashes, levels, brightness, stereo width and cost. Level against A: B within 0.3 dB in
every take; C within 0.35 dB except the single notes (−1.15 dB: its sparser keys are quieter,
the existing gain law, §6 per key). The stereo image of a chord changes measurably with the
profile; why it narrows in C has not been analysed:

| chord4 | A | B | C |
|---|---|---|---|
| RMS dB | −29.79 | −29.51 | −30.10 |
| side / mid energy | 1.50 | 1.22 | 0.76 |

### CPU and memory

`bench/phase1-cpu-linux.md`: the 15 baseline scenes, fastest of 5 runs per block.

| | Phase 0 | Phase 1, KEY DNA off | Phase 1, expressive on every layer |
|---|---|---|---|
| stress (3 × Granular, 8 notes, 294 grains), mean | 7.26 % | 7.55 % | 7.54 % |
| stress, worst block | 24.9 % | 25.8 % | 26.7 % |
| granular-chord8, mean | 2.14 % | 2.14 % | 2.20 % |

Every scene is within run-to-run noise: ±5 % relative, in both directions across scenes; off
vs. Phase 0 is the same code path plus one branch. Memory:
- the `InstrumentEngine` object grows by 49 512 bytes (three 128-key profiles plus their settings);
- each voice grows by 48 bytes (× 208 voices);
- renderer peak RSS is unchanged (26.4–26.9 MB).

Measured on this Linux VM only; Apple Silicon numbers come from CI's benchmark step, which does
not include these scenes yet.

## 7. Listening material (not in the repository)

- **Synthetic** (reproducible): `research-renderer --unified <dir> --set keydna` writes the 15
  demonstration WAVs.
- **Real recordings** (local corpus, never committed):
  `research-renderer --source <file> --config research/configs/keydna-<patch>.json --fixture chords|register`.
  Rendered for review on the owner's fiddle (Gåte × Arn, "Mi Jord") and nyckelharpa bounces.

Musical quality is not claimed from tests. It needs listening against baseline A and baseline
B, as CLAUDE.md requires; that is part of the review.

## 8. Known limitations

1. **Engine only.** There are no plugin parameters, no state and no GUI. The plugin cannot set
   KEY DNA yet; that is Phase 2 / Phase 5 by design.
2. A profile is fixed at note-on. Moving its settings while a note is held does not change that
   note (by design: no discontinuities). It is not yet a modulation destination; AMOUNT would be
   the first one, in Phase 2.
3. KEY DNA applies to Granular layers only. One Shot and REIMAGINED's One Shot granular
   continuation ignore it.
4. The key is the played (or arpeggiated) MIDI key. A mono glide keeps the first key's profile.
5. SCATTER and PAN are not parameters yet (approved for Phase 2, with the per-grain options).
6. Hashes are compared on linux-x86_64-gcc only; other platforms compare figures.
7. CPU was measured on a Linux VM; there are no Apple Silicon numbers for these scenes yet.

## 9. Updated roadmap (Phase 2 onward; nothing started)

The approved decisions are in brackets.

| Phase | Content |
|---|---|
| **2 Granulator options, budget, KEY DNA parameters** | <ul><li>Plugin parameters for KEY DNA (AMOUNT, INDIVIDUAL, SEED, per-parameter depth and variation, hint 20, neutral defaults), curves in state with their own version prefix, `stateVersion` 13 with migration (absent = off). KEY DNA AMOUNT as a modulation destination (A/B/C triples appended).</li><li>Per-grain SCATTER (pitch) and PAN on new streams, then added to KEY DNA [decision 4].</li><li>Window shapes, with today's rotation Hann as the default path.</li><li>Grain budget [decision 2]: unlimited for every session made before it exists; a finite, configurable default for new advanced configurations, set from benchmarks (including Apple Silicon). Fair share across sounding voices, deterministic, normalised by admitted density. A stress test bounds it; old sessions stay bit-identical.</li></ul> |
| **3 Note Atlas as a set source** | Region copies first [decision 5]; measure memory with realistic phrase lengths and note counts; shared-buffer regions only if that shows a meaningful gain. Atlas → `InstrumentSet`, FILL limits, `stateVersion` bump. |
| **4 Phrase analysis** | Dropped audio always loads as a normal sample first; no modal [decision 3]. MAP NOTES is a contextual action, and pitch analysis starts only when asked. An unobtrusive suggestion is allowed for long recordings that look melodic. The resulting atlas becomes a source in the existing InstrumentSet architecture. Evaluation on real recordings gates Phase 5. |
| **5 GUI** | KEY DNA and NOTE MAP pages from existing components (`KeyCurveEditor` extracted from DampenEditor and ModCurveView); OSP's layout unchanged; One Shot pixel-identical. |
| **6 Natural ↔ Grain** | In the first complete unified version, after the granular and atlas work is stable [decision 1]. GRAIN MIX is a small visible control in the source hover/editing strip, per layer A/B/C, modulatable. 0 % is today's natural playback, bit-identical, with the granular path skipped; 100 % is granular, with the read path skipped once settled. Blends use correlation-aware gain and the onset kept. No new macro. |
| **7 Optimisation and compatibility** | Mac profiling, budget tuning, the migration matrix |
| **8 Musical validation** | listening against baselines A and B |
