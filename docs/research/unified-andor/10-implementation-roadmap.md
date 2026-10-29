# 10 — Implementation roadmap (re-ordered by dependency)

> **Status:** Phase 0 and Phase 1 are done (phase-0-baseline.md, phase-1-key-dna.md). The
> approved product decisions and the updated plan for Phase 2 onward are in
> phase-1-key-dna.md §9; that table supersedes the phase descriptions below where they differ.

The brief's order is adjusted. KEY DNA comes first because it needs nothing else and proves
the central claim, that OSP's granulator can carry Grain's identity. The Atlas comes before
Phrase analysis because a set can be built from a hand-made atlas.

Every phase follows the same pattern:
- a git checkpoint on its own branch commit;
- "neutral is bit-identical" tests;
- rollback by reverting the phase's commits, because the parameters are new, neutral and never repurposed.

### Phase 0: Baseline and harness (1–2 days)

| | |
|---|---|
| **Goal** | Make granular playback, multi-layer scenes and key curves reachable headless (closes the Rule 4 gap). |
| **Reuse** | `src/research/Benchmark.*`, `RenderConfig`, the reference renders, `bench/bench_osp.cpp`. |
| **New** | Add `sourceMode`, `granular`, sets, curves and DAMPEN to `RenderConfig`. Fold `bench_osp` into `--benchmark --scene …`. |
| **Tests** | The existing suite, and research-renderer renders of granular scenes reproducing the plugin reference renders. |
| **Accept** | The reference scenes render identically from the CLI; the benchmark matrix is in the repo. |

### Phase 1: KEY DNA in the existing Granulator, engine only. **This is the first milestone.**

| | |
|---|---|
| **Goal** | Give each key a stable granular identity per layer, with no GUI. |
| **Reuse** | `andor-lab` `KeyDna.cpp` math, `notesGranular()`, the `NoteShape` pattern, `mod::Curve` / `CurveTable`. |
| **New** | `src/engine/KeyDna.{h,cpp}` (OSP parameter set; a new seed tag). A per-layer table in `InstrumentEngine` published lock-free. Offsets set at note-on. `EngineSettings` fields. RenderConfig support. |
| **Files** | `src/engine/{KeyDna.*, InstrumentEngine.*, InstrumentVoice.cpp (notesGranular)}`, `src/research/RenderConfig.*`, `docs/architecture.md`. |
| **Risks** | R1 (PRNG streams). |
| **Tests** | All reference renders bit-identical with DNA off. DNA determinism across block sizes. The same key gives the same identity; neighbouring keys' correlation follows INDIVIDUAL. Held notes keep their snapshot. No allocation (allocation-counting test as in the lab). |
| **Accept** | A/B renders with DNA on vs baselines A and B for listening; CPU unchanged within noise. |
| **Rollback** | Revert; no persisted state exists yet. |

### Phase 2: Granulator options and budget

> **Done** (phase-2-report.md). What was built differs from the plan below in three ways:
> - KEY DNA AMOUNT is the only host-automatable KEY DNA parameter. The profile-shaping
>   settings live in the session: automating them would make a bounce depend on when the
>   message thread recomputes.
> - Per-grain reverse chance was not in the Phase 2 brief and is deferred.
> - stateVersion 13 is taken by Phase 2, so Phase 3's atlas state becomes 14.

| | |
|---|---|
| **New** | Per-grain pitch scatter, pan and reverse chance (each on a new PRNG stream). Window shapes. `GrainBudget` with fair share and admitted-density normalisation. New parameters at hint 20 with neutral defaults. KEY DNA parameters added to the plugin's APVTS, with automation. |
| **Tests** | Bit-identical when neutral. A budget-on stress test bounds the grain count. Pan is mono-compatible. |
| **Accept** | The stress scene stays under the chosen budget; listening check. |

### Phase 3: Note Atlas as a set source

> **Done** (phase-3-report.md), with stateVersion 14 as planned. Beyond the plan: the atlas
> model gained asset origins (live captures and snapshots are first-class, paths are hints),
> velocity layers, note origins and FILL settings (schema 2); a session's missing recordings
> leave the layer empty and failed rather than restored; and the grain budget gained a
> `bounded` policy (never above the budget), the default for new sessions.

| | |
|---|---|
| **New** | `src/model/NoteAtlas.h` and `src/io/NoteAtlasJson.*` (ported). `src/engine/AtlasSet.*` (atlas → `InstrumentSet` with region copies). A FILL limit and assigned keys in `memberFor`. stateVersion 14 (13 is Phase 2's) with the atlas JSON per layer. Documentation in `analysis-schema.md`. |
| **Tests** | An atlas built from fixtures gives the expected key map. FILL limits hold, and a silent key steals no voice. A session round-trips. A state-12 session loads unchanged. |
| **Accept** | A hand-written atlas plays correctly in all three layers, One Shot and Granular. |

### Phase 4: Phrase analysis

| | |
|---|---|
| **New** | `src/analysis/phrase/*` (analysis, yin-viterbi, edits) using `core/Statistics`. Analysis cache in the Library store. `Classifier` hand-off: "Play as one sound / Map its notes". |
| **Tests** | The lab's analysis tests ported. **Evaluation on real recordings** (R4), which gates the next phases. |
| **Done** | phase-4-report.md: the analysis (version 3, doubts and verdicts), jobs and cache, corrections, staged preparation, capacity. The real-recording gate is partly met (spliced real phrases, real single-pitch takes); genuine performed phrases are still needed. Its recommendation moves short-note sustain and a re-articulation fix ahead of the GUI. |

### Phase 5: GUI (NOTE MAP, KEY DNA)

| | |
|---|---|
| **New** | `KeyCurveEditor` (extracted from DampenEditor and ModCurveView). The NOTE MAP page in the EQ \| DAMPEN family. The KEY DNA band takeover. Keyboard tints through `OspKeyboard` hooks. The NOTES and KEY DNA keys. |
| **Tests** | Screenshot scenes (`[canonical]`, `[ceramic-shots]`). A one-shot screenshot pixel-identical to today. Interaction tests. |

### Phase 6: Natural ↔ Grain hybrid (only if chosen)

| | |
|---|---|
| **New** | `layerX.grainMix` in One Shot. Path skipping at 0 and 1. A correlation-aware crossfade. Onset delay. |
| **Tests** | Bit-identical at 0. CPU benchmark of three hybrid layers. Loudness constant across the mix. |

### Phase 7: Optimisation and compatibility

Profile on a Mac. Decide on shared-buffer regions (R5). Tune the budget. Run the full migration matrix.

### Phase 8: Musical validation

Listening tests against baselines A and B, as CLAUDE.md requires. Realistic combinations: one-shot, atlas and granular in A/B/C.

**Deferred:**
- wavetable oscillator;
- multi-source atlases;
- velocity layers from Phrase loudness (they come for free from set inference, but need a listening check);
- polyphonic phrase analysis.
