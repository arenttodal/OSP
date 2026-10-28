# Unified engine, Phase 2: the Granulator's options, the grain budget, KEY DNA in the plugin

Phase 2 of the roadmap (10-implementation-roadmap.md) builds on the approved brief
(11-decision-brief.md) and the Phase 2 requirements. Like Phase 1, it **extends OSP's own
Granulator**: the existing engine, its baselines A/B and every established render are
unchanged. There is still no Phrase integration and no new GUI. **Stopped for review
before Phase 3.**

## 1. Summary

| Requirement | Result |
|---|---|
| 1 Live KEY DNA AMOUNT | AMOUNT now acts on sounding notes. Each voice follows it with a 30 ms control-rate one-pole that starts where AMOUNT is and lands exactly on a still target. It is a host parameter (`layerX.keyDna.amount`) and an engine modulation destination (`keyDnaAmountA..C`). The key's identity (its profile) is unchanged by AMOUNT and stays deterministic. AMOUNT 0 is bit-identical to no KEY DNA; AMOUNT 1 is the Phase 1 arithmetic exactly (every Phase 1 golden unchanged). |
| 2 Real-time profile safety | Profiles (128 keys x 7 parameters, 36 µs) are computed on the message thread and published as immutable `keydna::Profile` objects through a per-layer `ModelExchange`. The audio thread swaps a pointer once per block; superseded profiles are freed on the message thread. Tested: no allocation in the callback; newest-wins under bursts of 20 edits; garbage collected. Neutral behaviour is exact: no profile until one is set, and a disabled profile changes nothing. |
| 3 Stereo investigation | The expressive demonstration's narrower image comes from **POS, not SPREAD**: its keys read narrower stretches of the (very unevenly wide) test recording. A control recording that is equally wide all along shows no narrowing (side/mid 3.644 static vs 3.556 expressive, −2 %, against −50 % on the test vowel). This is intended behaviour, and the stereo algorithm is unchanged. §4. |
| 4 SCATTER, PAN, windows | `GranularParams.scatterSemitones` (±0..12 st), `.pan` (0..100 %, equal power) and `.window` (HANN / SOFT / PERCUSSIVE, energy-matched). They are neutral by default and draw from their own PRNG stream ("grop"), so the original grain choices never move. KEY DNA gains SCATTER and PAN. Old presets are unchanged (bit-identical). |
| 5 Grain budget | Deterministic **max-min fair** budget across A/B/C. Light notes keep every grain, the densest are thinned alike, and a thinned stream keeps its level. A budget that is not reached is bit-identical to none. Old sessions and presets open **Unlimited**. A sweep of 8 budgets x 2 stress materials gave **256** as the new-session default. §6. |
| 6 Parameters and state | 13 host parameters appended (version hint 20). The schema golden's first 350 lines are byte-identical; nothing was renamed, re-ranged or re-ordered, and the route-destination choice list keeps its 75 entries. stateVersion 13 adds a versioned `KeyDna` child (schemaVersion 1, round-trip-exact). v12 sessions migrate to Unlimited, neutral options and no KEY DNA. Save/reopen, migration and automation are tested. §7. |
| 7 Audio quality | Engine tests (17), plugin tests (8), real-recording renders (fiddle, nyckelharpa; 36 files) and synthetic demonstrations (16, pinned). Measurements are in §8. |
| 8 Compatibility and performance | Every Phase 0 and Phase 1 golden, every plugin reference (`[unified-baseline]`, `[reimagined-migration]`) and the full ctest of both trees pass (one existing assertion updated for the frozen destination list, §9.1). Costs and worst blocks are in §9; Apple Silicon figures come from CI (§9.3). |

## 2. What changed

**Engine (pure C++, `osp_dsp`).**

- `engine/KeyDna`:
  - SCATTER and PAN parameters (half ranges ±6 st and ±0.5);
  - the table now holds offsets at full AMOUNT;
  - `keydna::Profile { generation, table }`;
  - `apply (offsets, amount, params)` takes the live AMOUNT.
- `engine/GranularSource`:
  - SCATTER, PAN and WINDOW, with their own `optionRng`;
  - level-matched window tables;
  - a separate `renderShaped` loop, used only while a panned or table-windowed grain plays;
  - the per-stream grain cap and the level-preserving, gliding normalisation for it.
- `engine/InstrumentVoice`:
  - live, smoothed KEY DNA AMOUNT;
  - per-stream caps;
  - grain demand and count.
- `engine/InstrumentEngine`:
  - `setLayerKeyDnaProfile` / `setLayerKeyDnaAmount` (real-time safe);
  - `setLayerKeyDna` (offline and tests; owned storage);
  - `setGrainBudget` and `applyGrainBudget()` at the start of every `render`.
- `engine/Modulation`: `keyDnaAmountA..C` destinations (appended), and
  `mod::hostDestCount` = 75, the plugin's frozen list.

**Research harness.**

- `RenderConfig`:
  - `granular.scatterSemitones / pan / window`;
  - KEY DNA `scatter` and `pan`;
  - `keyDnaAmount` automation lanes;
  - root `grainBudget`.
- `UnifiedScenes`:
  - `phase2Scenes()` (16) and `budgetScenes()` (16);
  - a mono vowel source;
  - costs now include `peakLiveGrains` (every stream) and `worstObservedMicros` (the
    slowest block of any run).
- `research-renderer --unified --set phase2|budget`; configs in `research/configs/phase2-*.json`.

**Plugin.**

- Parameters (§7).
- KEY DNA settings and profile exchange in `OspAudioProcessor`:
  - `setKeyDnaSettings` / `keyDnaSettings`;
  - `swapKeyDnaIfPending()` at the start of `processBlock`;
  - garbage collection on the timer.
- The `KeyDna` state child; stateVersion 13 and its migration.
- `ShellProcessor` products inherit the appended parameters. They ignore them, and the
  products' visual regressions are unchanged.

## 3. Live KEY DNA AMOUNT (requirement 1)

**Is it possible without compromising compatibility or real-time safety? Yes, with one change
of approach.** Phase 1 baked AMOUNT into the table, which notes copied at note-on: changing it
required recomputing all keys and affected only new notes. Phase 2 separates the two:

- **identity**: the profile (each key's offsets at full AMOUNT). It is deterministic from
  the seed and the settings, unchanged by AMOUNT, and copied into the voice at note-on as
  before.
- **amount**: a number the voice reads at control rate (every 32 samples). The target is
  the layer's AMOUNT plus any `keyDnaAmount` route, clamped to 0..1.

The voice's applied amount follows the target with a one-pole of 30 ms. It starts at the
target when the note starts (no glide in) and snaps once within 1e-5, so:

- **AMOUNT 1 is exactly Phase 1.** `apply` uses the offsets unscaled at amount >= 1. All 15
  Phase 1 demonstration goldens are bit-identical.
- **AMOUNT 0 is exactly no KEY DNA.** `apply` returns before touching anything. Tested in
  the engine (automation lane at 0 vs KEY DNA off) and in the plugin (host parameter at 0 vs
  no profile).
- **Determinism.** Automation is applied per block from the host's timeline, and the
  smoothing is per voice from fixed constants. Renders are bit-identical run to run at any
  fixed block size. Like everything in OSP (Phase 0 finding), they are not block-size
  invariant.
- **The identity is untouched.** A test dips AMOUNT on one note. After it has gone, the same
  key is played again, and its audio is bit-identical to the take where AMOUNT never moved.

Measured on the stepped scene (0 -> 100 % at 1.5 s, 100 -> 0 % at 3.5 s, a held chord),
largest sample-to-sample step in the 300 ms around each step:

| | step | AMOUNT held at 0 | AMOUNT held at 100 % |
|---|---|---|---|
| 0 -> 100 % at 1.5 s | 0.0344 | 0.0471 | 0.0369 |
| 100 -> 0 % at 3.5 s | 0.0306 | 0.0385 | 0.0371 |

The step is no larger than what the chord does anyway, so there is no click. Smoothing
matters most for SIZE and DENS, whose level normalisation (and hence gain) moves
immediately when they change.

**Modulation.** `keyDnaAmountA..C` are real engine destinations (an LFO sweep is one of the
demonstrations). The plugin's route-destination choice parameter is deliberately not
extended: a choice parameter's normalised value is its index divided by the count, so
appending entries would move every saved route's automation. `mod::hostDestCount` freezes
the plugin list at 75. **Decision for you:** expose KEY DNA AMOUNT as a route destination
with the Phase 5 GUI, in a new parameter or a new routing scheme.

## 4. Stereo investigation (requirement 3)

The question: why does the expressive demonstration (C) have lower side/mid energy than the
static one (A)? Is it SPREAD, or an unintended interaction? Investigation:
`tests/investigation/StereoInvestigation.cpp` (hidden, `osp_tests "[.][stereo-investigation]"`).
One thing is changed at a time.

**1. The test recording is very unevenly wide.** Side/mid of the vowel by 10 % of its length:

| 0 % | 10 % | 20 % | 30 % | 40 % | 50 % | 60 % | 70 % | 80 % | 90 % |
|---|---|---|---|---|---|---|---|---|---|
| 1.714 | 0.512 | 0.563 | 2.690 | 1.104 | 0.404 | 1.251 | 2.052 | 0.743 | 0.222 |

**2. Four-note chord, one change at a time** (side/mid):

| variant | side/mid |
|---|---|
| A static | 1.504 |
| C expressive (all) | 0.759 |
| C, POS only | 0.612 |
| C, SIZE only | 1.607 |
| C, DENS only | 1.533 |
| C, SPREAD only | 1.277 |
| C, all but SPREAD | 0.640 |
| C, all but POS | 1.252 |
| A, SPACE and LIFE off | 1.304 |
| C, SPACE and LIFE off | 0.803 |
| C, POS only, SPACE and LIFE off | 0.645 |

**3. Each chord key alone** (SPACE and LIFE off). The recording's own width is measured over
the stretch each key's grains read: POS ± (SPREAD / 2 of the recording + 4 ms).

| key | rendered A -> C | POS A -> C | SPREAD A -> C | recording over its read region A -> C |
|---|---|---|---|---|
| 60 | 1.279 -> 1.235 | 0.450 -> 0.670 | 0.150 -> 0.221 | 1.115 -> 1.305 |
| 64 | 1.105 -> 0.592 | 0.450 -> 0.529 | 0.150 -> 0.236 | 1.115 -> 0.702 |
| 67 | 1.093 -> 0.480 | 0.450 -> 0.524 | 0.150 -> 0.168 | 1.115 -> 0.513 |
| 71 | 1.235 -> 0.676 | 0.450 -> 0.570 | 0.150 -> 0.245 | 1.115 -> 0.689 |

Each key's rendered width follows the recording's width over the stretch it reads. Key 60
moves to a wider stretch and stays wide; 64, 67 and 71 move to narrower ones and narrow.

**4. Control.** A recording that is equally wide all along: the vowel's left channel, with
the right channel a 0.7 ms-delayed copy. Its side/mid is 3.71–3.80 in every 10 % window. Four-note chord: A static 3.644, C expressive 3.556 (−2 %, against −50 % on the test vowel).

**Conclusion.**
- The narrowing comes from **POS**. KEY DNA C moves each key to its own place in the
  recording, and on this recording those places happen to be narrower (POS only gives 0.61).
- SPREAD's own variation barely contributes: removing it leaves 0.64, and alone it gives
  1.28. Wider SPREAD averages over more of the recording, so it pulls a key's width toward
  the recording's mean rather than narrowing it.
- With a recording whose width does not change along its length, the effect disappears.

**This is the intended result of "each key reads its own part of the recording"**, not a
stereo bug. The stereo algorithm is unchanged. On real recordings, POS-driven width changes
follow the recording: the fiddle is nearly mono everywhere (side/mid 0.14), and expressive
KEY DNA hardly changes its width.

## 5. SCATTER, PAN and the windows (requirement 4)

| Option | What it does | Neutral | Stream |
|---|---|---|---|
| SCATTER | each grain's pitch moves randomly within ±semitones (0..12) | 0: not drawn | `optionRng` ("grop") |
| PAN | each grain's place within ±PAN of the field; equal power (√2·cos / √2·sin of a quarter turn), unity at the centre; stereo grains are balanced, not summed to mono | 0: not drawn | `optionRng` |
| WINDOW | HANN (the original rotation), SOFT (Tukey, flat middle half), PERCUSSIVE (4 % rise, exponential fall, 4 % taper); SOFT and PERCUSSIVE are 1025-point tables built at load time and energy-matched to Hann (mean square 0.375) | HANN | none |

- **Old presets are unchanged.** With SCATTER 0, PAN 0 and HANN, no option code runs: the
  options draw nothing, and the original render loop is used while no shaped grain plays.
  `p2-options-neutral-chord4` sets them explicitly and is bit-identical to Phase 1's
  `keydna-a-static-chord4`. The plugin test does the same through the host parameters.
- **Independent streams.** With PAN or a table window on, every grain's start, read
  position and display lane are identical to the original, block by block (tested). SCATTER
  changes where a grain reads next, because its pitch changes, but never when grains start
  or end: the grain counts are identical block by block.
- **KEY DNA.** SCATTER and PAN are KEY DNA parameters too, keyed per note. The
  demonstration: low keys steady and centred, high keys scattered and wide; key 84 measured
  wider than key 40.

Measurements:

| Measurement | Result |
|---|---|
| PAN, mono recording (held note), side/mid | PAN 0: 0.006 (SPACE's reverb only); 50 %: 0.066; 100 %: 0.270 |
| PAN, mono recording, L+R energy at 100 % | −0.04 dB |
| PAN, mono recording, mono sum | 50 %: −0.27 dB; 100 %: −1.05 dB |
| PAN, stereo test vowel, side/mid | 2.363 -> 2.106 -> 1.603 |
| PAN, stereo test vowel, L+R energy | −0.02 dB |
| Windows against HANN, four-note chord | SOFT −0.01 dB, PERCUSSIVE −0.10 dB |
| Windows against HANN, sparse scale (200 ms, 8/s) | SOFT −0.02 dB, PERCUSSIVE +0.90 dB (few grains: its peakier shape shows) |
| SCATTER ±3 st, four-note chord | level −0.13 dB, brightness 0.250 -> 0.256 |

- **PAN on a mono recording** widens from exactly centred. L+R energy is preserved and the
  mono sum loses about 1 dB at full PAN.
- **PAN on the (anti-correlated, side/mid ≈ 2) stereo test vowel** moves the image rather
  than widening it: a hard-panned stereo grain is one channel of the recording. This is a
  property of balance panning, documented and tested.
- **On the real recordings** PAN 60 % widens the fiddle from 0.139 to 0.210 (+51 %) and the
  nyckelharpa from 0.407 to 0.464. The level changes by 0.0 to 0.1 dB.

## 6. The grain budget (requirement 5)

**Algorithm**, once per block, at the start of `InstrumentEngine::render`, from the voices
alone (deterministic, never from timing):

1. Every sounding grain stream (a granular voice, and KALEIDOSCOPE's second stream) asks for
   `ceil (DENS x SIZE) + 1` grains, at most 24 (the pool).
2. The share is the largest per-stream cap whose total, `Σ min (asked, cap)`, fits the
   budget. This is max-min fairness:
   - light notes keep every grain;
   - one dense note cannot starve the others;
   - the densest notes are thinned alike.
   The integer remainder goes one grain each to thinned streams in voice order, so shares
   differ by at most one.
3. If the total asked fits, there is no cap at all, so **a budget that is not reached is
   bit-identical to none** (tested at 2000 and 512 on the stress scene).
4. A thinned stream keeps the level it would have had: norm = √asked · law(asked) / √admitted.
   The law is OSP's original 1/max(1, 0.612·√overlap).
   - A stream capped mid-note starts from the grains it is actually playing and glides over
     about half a grain.
   - A note started under a cap starts at its admitted level. There is no step when shares
     change: the largest 10 ms level change when 7 notes join a dense held note is 3.64 dB,
     against 3.22 dB unlimited (the entry itself).
5. The floor is one grain per stream: more streams than the budget exceed it.

Spawns denied at the cap are simply skipped. The CPU stays bounded because the pool
(24 per stream) still applies.

**Fairness test.** Six dense notes (20 grains each) on A and six light notes (2 each) on B,
budget 60. B's grains are bit-identical to the unlimited render. A's six notes play between
6 and 7 (the share is 8) grains each.

### 6.1 Benchmark and the default

Two materials:
- **stress**: the Phase 0 CPU stress scene, A/B/C Granular, SIZE 250–350 ms, DENS 32–40,
  an eight-note chord; about 294 grains;
- **extreme**: every layer at the pool (DENS 40 x SIZE 600 ms) with KALEIDOSCOPE's second
  stream, 48 streams; 993 grains.

Each was swept over eight budgets. This is linux-x86_64-gcc, the container's CPU, a 256-sample
block at 48 kHz (5.33 ms). Mean and worst are the fastest of 3 runs per block. "Worst seen" is
the slowest block of any run: it includes the container's scheduling noise and is reported,
not used.

| Scene | Budget | Mean % | p99 µs | Worst µs | Worst seen µs | Grains at most | Level vs unlimited | Peak vs unlimited |
|---|---|---|---|---|---|---|---|---|
| stress | Unlimited | 7.44 | 1368 | 1604 | 14632 | 294 | — | — |
| stress | 512 | 7.18 | 1285 | 1341 | 14857 | 294 | +0.00 dB (identical) | +0.00 dB |
| stress | 384 | 7.24 | 1317 | 1418 | 2351 | 294 | +0.00 dB (identical) | +0.00 dB |
| stress | 256 | 6.79 | 1206 | 1266 | 3211 | 235 | +0.25 dB | +1.28 dB |
| stress | 192 | 6.36 | 1135 | 1189 | 2104 | 183 | +0.29 dB | +1.42 dB |
| stress | 128 | 5.53 | 1071 | 1214 | 6156 | 126 | +0.32 dB | +2.31 dB |
| stress | 96 | 4.97 | 990 | 1040 | 1771 | 95 | +0.68 dB | +2.52 dB |
| stress | 64 | 4.65 | 959 | 1003 | 1387 | 64 | +0.92 dB | +1.91 dB |
| extreme | Unlimited | 16.46 | 2580 | 2686 | 14520 | 993 | — | — |
| extreme | 512 | 13.01 | 1956 | 2116 | 6106 | 506 | +0.18 dB | +1.04 dB |
| extreme | 384 | 11.85 | 1747 | 1812 | 5860 | 384 | -0.06 dB | -0.04 dB |
| extreme | 256 | 10.39 | 1593 | 1814 | 2443 | 256 | -0.52 dB | +0.23 dB |
| extreme | 192 | 9.68 | 1529 | 1606 | 5945 | 192 | -0.64 dB | +0.17 dB |
| extreme | 128 | 8.98 | 1418 | 1449 | 5030 | 128 | -0.83 dB | -0.59 dB |
| extreme | 96 | 8.71 | 1397 | 1451 | 3121 | 96 | -0.45 dB | +0.81 dB |
| extreme | 64 | 8.01 | 1293 | 1368 | 3218 | 64 | -0.62 dB | +1.28 dB |

Reading it:
- The budget only binds when reached: on the stress scene, 512 and 384 are bit-identical to
  Unlimited.
- 256 trims the stress scene by 0.25 dB, playing 235 of its 294 grains.
- 256 takes the extreme scene from 16.5 % to 10.4 % mean and its worst block from 2.69 ms
  to 1.81 ms.
- Below 128 the savings flatten: about 8 % of the extreme scene's cost is the voices' other
  work (filters, REIMAGINED, SPACE). Meanwhile the sparse grains become audible as grain:
  peaks rise by 1.3–2.5 dB on the stress scene, because fewer, louder grains are less smooth.

**Default for new sessions: 256 grains.** It never touches realistic patches (the 8-note
stress chord asks for 294 and loses 59), and it cuts the pathological case's CPU by 37 %. It
is a choice parameter, so **Unlimited, 512, 384, 256, 192, 128, 96 and 64** are all one
click away. Older sessions and every preset saved before Phase 2 open Unlimited.

**Decision for you:** confirm 256 once the Apple Silicon figures (§9.3) are in, or pick
another step.

## 7. Parameters and state (requirement 6)

**Host parameters**, appended at the end of the layout (version hint 20), so nothing before
them moves:

| ID | Range | Default |
|---|---|---|
| `layerA/B/C.granular.scatter` | 0..12 st (0.01) | 0 ("Off") |
| `layerA/B/C.granular.pan` | 0..100 % | 0 |
| `layerA/B/C.granular.window` | Hann, Soft, Percussive | Hann |
| `layerA/B/C.keyDna.amount` | 0..100 % | 100 % (heard only once the layer has a profile) |
| `grainBudget` | Unlimited, 512, 384, 256, 192, 128, 96, 64 grains | 256 (index 3); older sessions: Unlimited |

- `tests/plugin/golden/osp-parameter-schema.txt` was regenerated: lines 1–350 are
  byte-identical and 13 lines were appended.
- `mod.routeN.dest` keeps its 75 choices.

**Why only AMOUNT is automatable (a deviation from the roadmap's "KEY DNA parameters with
automation").** A KEY DNA profile (seed, individuality, each parameter's depth, variation
and curve) is rebuilt for all 128 keys whenever it changes. Requirement 2 rules out doing
that on the audio thread. If those settings were host parameters, their automation would be
applied whenever the message thread happened to recompute. An offline bounce, where the
message thread does not run between blocks, would then differ from playback, which breaks
`seed -> exact repeatable output` (CLAUDE.md Rule 2). AMOUNT is applied on the audio thread
itself, deterministically. So the profile lives in the session, edited through
`setKeyDnaSettings` (and Phase 5's GUI), and AMOUNT is the automatable part. If you want
automatable profile controls later, they would need a different design, for example a small
set of precomputed profiles crossfaded on the audio thread.

**State**:
- `stateVersion` 13 adds a `KeyDna` child with `schemaVersion` 1. Per layer it stores
  `enabled`, `individuality` and `seed`; per non-neutral parameter `depth`, `variation` and
  the compiled `curve`. Numbers are round-trip-exact text (`%.17g`; curve values `%.9g`).
  See analysis-schema.md.
- A missing child, or one from another schemaVersion, loads as no KEY DNA. Nothing is
  guessed.

**Migration** (sessions saved before 13):
- `grainBudget` -> Unlimited;
- the new options -> neutral (they are absent, so the existing "absent parameter -> its
  default" rule applies);
- no KEY DNA.

**Tests** (`tests/plugin/UnifiedPhase2Tests.inc`):

| Test | What it checks |
|---|---|
| Appended parameters | IDs, order, defaults and the frozen destination list |
| v12 session | Converted from a new one: reopens Unlimited and neutral, with no KEY DNA. It sounds different with the 256 budget (the budget binds on that session) and is bit-identical once the budget is set to Unlimited. |
| KEY DNA round trip | Three layers saved and reopened give equal settings and bit-identical audio; the audio changes without KEY DNA; a schemaVersion 99 child is refused. |
| Exchange | No profile at first; a published profile is not taken until the next block; newest wins after 20 rapid edits; old profiles are freed by the timer. |
| AMOUNT automation | Host automation 0 -> 100 % on a held chord is deterministic and differs from both ends; AMOUNT 0 is identical to no KEY DNA. |
| Grain options | SCATTER, PAN and WINDOW reach the grains; explicit neutral is the original. |
| Budget | 512 on a ~400-grain chord is the original; 64 differs. |
| Real-time safety | The audio callback allocates nothing while profiles change and AMOUNT is automated. The allocation counter now counts per thread, so the plugin's loader and library threads don't interfere. |

## 8. Audio quality (requirement 7)

**Engine tests** (`tests/integration/GrainPhase2Tests.cpp`, 17 cases):
- neutral identity;
- independent streams for PAN and the windows, and grain timing under SCATTER;
- PAN width, energy and mono sum (mono and stereo sources);
- window level matching;
- extremes stay finite and below 0 dBFS;
- KEY DNA SCATTER and PAN by key;
- AMOUNT: smoothing (monotonic, about 30 ms, lands exactly), automation determinism, AMOUNT
  0 = off, a step does not click, identity after a dip, the modulation route, no allocation;
- the budget: never-reached = original, deterministic, bounded and used, level within 2 dB,
  fair, the level glides on a share change, no allocation.

**Real recordings.** These are your own: the fiddle ("Mi Jord") and a nyckelharpa bounce,
not the commercial violin library. Two fixtures (chords, register), nine variants each,
against the Phase 1 static patch:

| Variant | fiddle chords RMS / peak / width | fiddle register | nyckelharpa chords | nyckelharpa register |
|---|---|---|---|---|
| static (Phase 1 A) | -40.31 / -21.73 / 0.139 | -45.10 / -26.29 / 0.142 | -37.70 / -19.88 / 0.407 | -43.36 / -26.97 / 0.376 |
| SCATTER ±3 | -40.26 / -19.14 / 0.137 | -45.04 / -27.05 / 0.133 | -37.75 / -19.74 / 0.403 | -43.30 / -27.00 / 0.378 |
| PAN 60 % | -40.32 / -21.16 / 0.210 | -45.02 / -24.85 / 0.228 | -37.72 / -20.93 / 0.464 | -43.43 / -24.89 / 0.456 |
| SOFT | -40.25 / -22.01 / 0.139 | -45.07 / -25.20 / 0.144 | -37.72 / -21.43 / 0.405 | -43.41 / -27.13 / 0.375 |
| PERCUSSIVE (200 ms, 8/s) | -41.26 / -19.76 / 0.138 | -45.27 / -22.23 / 0.141 | -38.56 / -17.99 / 0.406 | -44.40 / -23.32 / 0.350 |
| KEY DNA SCATTER+PAN | -40.32 / -20.49 / 0.162 | -44.99 / -26.67 / 0.146 | -37.77 / -21.32 / 0.413 | -43.34 / -26.52 / 0.396 |
| AMOUNT 0→1→0 (expressive) | -40.74 / -20.62 / 0.142 | -43.76 / -22.67 / 0.141 | -39.21 / -21.91 / 0.375 | -44.44 / -24.07 / 0.359 |
| dense (400 ms, 40/s), Unlimited | -38.95 / -21.80 / 0.143 | -44.19 / -26.96 / 0.154 | -37.88 / -22.11 / 0.386 | -42.04 / -25.35 / 0.362 |
| dense, budget 64 | -38.89 / -20.62 / 0.142 | -44.19 / -26.96 / 0.154 | -37.62 / -20.67 / 0.383 | -42.04 / -25.35 / 0.362 |

RMS and peak in dBFS; width is side/mid energy.

- Levels stay within about 1 dB of static.
- The PERCUSSIVE rows use sparser grains (200 ms, 8/s), so their level and peaks differ by
  design.
- The register sweep plays one note at a time, so the 64-grain budget never binds there: it
  is bit-identical to Unlimited.

**Polyphony:** eight-note chords (KEY DNA SCATTER and PAN, the budget scenes) and A/B/C
three-layer stress. **Held notes:** the AMOUNT automation, step and LFO scenes (5 s chords).

## 9. Compatibility and performance (requirement 8)

### 9.1 Compatibility

| | |
|---|---|
| Phase 0 baseline (15 engine scenes, exact on linux-x86_64-gcc) | unchanged |
| Phase 1 KEY DNA demonstrations (15) | unchanged |
| Plugin `[unified-baseline]` (3 sessions), `[reimagined-migration]` | unchanged |
| Parameter schema | first 350 lines identical, 13 appended |
| Full ctest, engine tree and plugin tree | pass (engine tree 5/5; plugin tree 9/9 after one assertion was updated, see below) |

New pinned goldens are in `tests/regression/golden/phase2` (16 demonstrations).

**One existing assertion was updated.** The MODULATION test checked that the plugin's
destination names equal every engine destination. It now checks that they equal
`mod::hostDestCount` (75), below `mod::destCount` (78). That is the deliberate freeze from §3:
the engine has destinations the plugin's routes don't offer yet.

### 9.2 Costs (linux-x86_64-gcc)

| Scene | Mean % | Worst block % | Worst block µs | Worst seen µs | Grains at most |
|---|---|---|---|---|---|
| p2-options-neutral-chord4 | 5.16 | 10.82 | 577 | 2324 | 11 |
| p2-scatter-3-chord4 | 5.19 | 9.42 | 502 | 1508 | 11 |
| p2-scatter-12-single | 4.41 | 9.46 | 504 | 777 | 3 |
| p2-pan-50-chord4 | 5.11 | 9.57 | 510 | 3374 | 11 |
| p2-pan-100-single | 3.93 | 9.87 | 526 | 4589 | 3 |
| p2-pan-100-mono-single | 3.86 | 9.40 | 501 | 3519 | 3 |
| p2-window-soft-chord4 | 5.40 | 9.66 | 515 | 3248 | 11 |
| p2-window-percussive-chord4 | 5.14 | 9.24 | 493 | 1362 | 11 |
| p2-window-percussive-sparse-scale | 4.24 | 9.47 | 505 | 3256 | 3 |
| p2-keydna-scatter-pan-scale | 4.38 | 9.37 | 500 | 1571 | 4 |
| p2-keydna-scatter-pan-chord8 | 5.55 | 8.54 | 456 | 2626 | 21 |
| p2-amount-automation-held-chord | 4.70 | 9.21 | 491 | 1850 | 10 |
| p2-amount-step-held-chord | 4.61 | 9.23 | 492 | 4259 | 10 |
| p2-amount-lfo-held-chord | 5.10 | 10.10 | 539 | 2737 | 9 |
| budget-stress-unlimited | 7.27 | 25.77 | 1374 | 2385 | 294 |
| budget-stress-128 | 5.49 | 20.24 | 1079 | 1999 | 126 |

256-sample blocks at 48 kHz (5.33 ms). Mean and worst keep the fastest of 3 runs per block;
"worst seen" is the slowest block of any run and mostly shows the container's scheduling.
Files: bench/phase2-demos-linux*, bench/phase2-budget-sweep-linux*.

- **Memory.** The engine object grew from 108 256 to 127 568 bytes (+19 KB: KEY DNA's owned
  tables and their profile pointers). Each voice grew from 19 440 to 20 848 bytes (+1.4 KB:
  the PAN gains and window state of 2 x 24 grains), about +290 KB of heap per engine across
  its 208 voice slots.

- The new options cost nothing measurable on a four-note chord: 5.11–5.40 % against 5.16 %
  neutral, within run-to-run noise. PAN and the table windows use the general loop only
  while such grains play.
- KEY DNA AMOUNT adds one control-rate one-pole per granular voice.
- The budget adds one pass over the voices per block.
- A profile computation (36 µs, message thread) never runs on the audio thread.

### 9.3 Apple Silicon

No Apple machine is available in this environment. CI's macOS arm64 job now runs
`research-renderer --unified --set phase2` and `--set budget` and prints both summaries to
its log (step "Unified engine costs"); the figures from the first run after this commit are
added below when that run completes.

## 10. Demonstrations

- **The listening page.** `phase2-listening.zip` contains `index.html` and MP3s, with each
  row level-matched as a group:
  - synthetic: the options, extremes, KEY DNA SCATTER/PAN, live AMOUNT, the budget;
  - recordings: fiddle and nyckelharpa.
- **Reproduce:**
  - `research-renderer --unified <dir> --set phase2` (and `--set budget`);
  - `research-renderer --source <file> --config research/configs/phase2-<name>.json --fixture chords|register`.
- The recordings stay local: no corpus audio is in the repository.

## 11. For review before Phase 3

1. **Grain budget default:** 256 for new sessions (§6.1). Confirm, or choose another step.
2. **KEY DNA automation scope:** only AMOUNT is a host parameter (§7). Is that right, or do
   you want automatable profile controls (a different design)?
3. **KEY DNA AMOUNT as a route destination** in the plugin: postpone to Phase 5 (it needs a
   new route parameter or scheme, §3)?
4. **PAN on stereo recordings:** balance (keeps each grain's image, as now) or summing each
   grain to mono before placing it (always widens, loses the recording's own stereo)?
5. **Per-grain reverse chance** (in the roadmap's Phase 2 list, not in this brief): deferred.
   Wanted in a later phase?
6. **Phase 3** (Note Atlas as a set source) will use stateVersion 14.
