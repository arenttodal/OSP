# 11 — Decision brief: one unified source engine for ANDOR OSP

Research only; no production DSP or GUI was changed. Sources:
- OSP `claude/charming-lovelace-n25fte` at `db136e5`.
- The prototypes on `origin/andor-lab` at `54afd07`, extracted read-only. The lab's engine tests ran here: 43 cases, all passing.
- Measurements from a 4-vCPU Xeon VM (see 09). They do not stand for Mac numbers.

## Executive summary

**A. Feasibility.** Feasible with the current codebase, without a rewrite. OSP already has the two hard pieces the brief asks for:
- a **per-voice polyphonic granulator** (`GranularSource`, one per voice);
- a **multi-region source model** (`InstrumentSet`: pitch groups, velocity layers, round-robin, region choice per note).

What the prototypes contribute is mostly data and analysis:
- a KEY DNA table;
- a Note Atlas;
- phrase segmentation;
- a few granular options and a grain budget.

The main obstacles:
- Phrase analysis has been tested only on synthetic audio.
- PRNG and parameter compatibility rules must be kept exactly.
- Space in the GUI is tight in three-layer mode.
- A hybrid Natural+Grain voice costs both paths.

**B. Recommended architecture.** **A: Minimal OSP extension** (04). Port KEY DNA, the atlas, the phrase analysis and the grain options into OSP's existing seams:
- `notesGranular()`;
- `memberFor()`;
- the set builder;
- `GranularSource::spawn`.

Do not transplant the Grain or Phrase engines.
- B (one unified voice) touches the locked per-sample chain.
- C (two engines behind adapters) means two granulators, two modulation matrices and two pools, and still needs a legacy path.

**C. Granulator decision.** The existing Granulator stays and grows. It becomes the only granular engine.
- KEY DNA and every new option are amounts whose neutral value is today's behaviour.
- New randomness draws from new seed streams.
- The budget is unlimited by default.

The result: existing presets render bit-identically, and "advanced" is the same DSP path with a per-key profile. No second switch is needed.

**D. Phrase decision.** A Note Atlas is a new way to **build an `InstrumentSet`**: each included note becomes a member, variations become takes, and an assigned key becomes the member's root. Playback is unchanged:
- One Shot uses OSP continuation per note.
- Granular grains stay inside the note's region.

Additions:
- a FILL limit in `memberFor`;
- the atlas JSON stored per layer (state 13).

Region audio is copied per member at first. Shared-buffer regions come later, only if memory requires it. A one-shot stays exactly what it is.

**E. GUI decision.** The layout and skin stay unchanged. Four new things are added:
- a **NOTES** key on atlas layers only, which opens a NOTE MAP page beside EQ | DAMPEN;
- a **KEY DNA** key at the end of the existing granular hover strip, which opens a band-takeover editor aligned to the real keyboard;
- passive note regions on the waveform;
- keyboard tints.

A dropped one-shot shows none of these. See the mockups in 08.

**F. Performance.**
- Per voice, OSP's granulator and Grain cost the same here: about 5 % for an 8-note cloud on this VM.
- The risk is OSP's unbounded worst case. Three granular layers with 16 notes and 16 routes reached 384 grains, at 40 % mean and 81 % p99.
- Port Grain's budget.
- Atlas sets are cheap.
- The hybrid voice and Mac numbers still need benchmarking.

**G. Roadmap.**
- **First:** a headless harness (Phase 0), then **KEY DNA in the engine (Phase 1)**.
- **Second:** granular options and the budget (2), then the Atlas as a set source (3).
- **Third:** phrase analysis with real-recording evaluation (4), then the GUI (5).
- **Then:** the hybrid (6), optimisation (7) and listening validation (8).

**H. Open decisions (need you).** See the end of this brief.

## Answers to the 14 questions

1. **Can the OSP Granulator realistically evolve into the Grain engine?**
   - Yes. It is already per voice and polyphonic.
   - The real gaps are per-key identity, a global budget, per-grain pitch scatter, pan and reverse, window shapes, region-bounded reads (which atlas members provide) and an oscillator.
   - The first four are additive.
2. **Reuse Grain's DSP, extend OSP, or share a core?**
   - Extend OSP. Reuse Grain's `KeyDna` math almost unchanged, and port its budget and per-grain options.
   - Drop its voice shell, its modulation matrix and its effects wrapper: they duplicate OSP and are weaker (pitch is frozen per grain, and FOLLOW, doubling and SIMD are missing).
3. **One system for static and per-key behaviour?**
   - Yes. Static is KEY DNA at 0.
   - The legacy guarantee holds when four rules are kept:
     - new PRNG streams only;
     - neutral offsets act as exact identities;
     - no budget denial by default;
     - the rotation Hann stays the default window.
4. **Cleanest way to add the Note Atlas?**
   - Build an `InstrumentSet` from the atlas.
   - Add a FILL limit and assigned-key roots to `memberFor`.
   - Persist the atlas per layer.
   - Leave the engine's voice path untouched.
5. **Natural and Granular at once without excessive CPU or phase problems?**
   - Yes, as GRAIN MIX inside One Shot. KALEIDOSCOPE already does a version of this.
   - Skip the paths at 0 and 1, and use an equal-power or correlation-aware crossfade.
   - Latency and pitch match, because both paths run in one voice.
   - The cost is about one-shot plus granular per voice. Estimated, not yet measured.
6. **Should KEY DNA share the NOTE curve editor?**
   - Yes. Use one extracted `KeyCurveEditor` and one `mod::Curve` model for both.
   - The semantics stay separate: NOTE is a mod source, KEY DNA a per-key definition.
7. **A, B and C all with atlases and granular?**
   - Each layer resolves its own member per note, and each voice runs its own grains.
   - Shared: the 208-voice pool (24 per layer) and the proposed global grain budget with fair share.
   - Measured: a set plus two granular layers at 8 notes cost 11.6 %.
8. **Voices and grain limits?**
   - Keep the per-layer polyphony.
   - Add one engine-level grain budget across A/B/C with a deterministic fair share.
   - Normalise gain by the admitted density.
   - Unlimited by default; a finite value is opt-in.
9. **Wavetable in the first integration?**
   - **Defer.** It does not serve "drop any audio and play it", it adds UI and a third source concept, and the prototype's own known issues (no imported tables) show it is unfinished.
   - The bank can be reused later as is.
10. **Minimal GUI changes?** Two context keys (NOTES, KEY DNA), two pages built from existing components, passive regions and keyboard tints. Nothing changes for a one-shot.
11. **Safest migration?**
    - Never rename, repurpose or re-range existing IDs, and never add choices to existing choice parameters.
    - Use new parameters at hint 20 with neutral defaults (missing parameters already reset to defaults on load).
    - Bump stateVersion to 13 for the atlas and DNA curves, keeping the c1–c3 curve decoders.
    - Use new seed tags.
    - The gate: every reference render in `tests/audio/reimagined-before` must stay bit-identical.
12. **Reuse, adapt, rewrite or defer** (details in 02/03):

    | Part | Verdict |
    |---|---|
    | `KeyDna` | reuse |
    | Note Atlas and JSON | adapt |
    | `PhraseEdits` | reuse |
    | yin-viterbi, segmentation | adapt |
    | Budget, per-grain options, FILL limits | adapt |
    | `GrainEngine` and `PhraseEngine` voices, matrix, SharedFx, `AndorStore` | do not reuse |
    | Wavetable, the prototypes' editors | defer (the editors serve as interaction references) |
13. **Deliberately not in the first version:**
    - the wavetable oscillator;
    - a second granular engine;
    - a second modulation matrix;
    - shared-buffer regions;
    - multi-source atlases;
    - polyphonic phrase analysis;
    - per-layer KEY DNA editing in three-layer mode beyond the takeover editor;
    - any change to the existing ranges of POS, SIZE and DENS.
14. **Smallest meaningful milestone: Phase 1, KEY DNA in OSP's granulator, engine only.**
    - A per-layer `KeyDnaTable` (POS, SIZE, DENS, SPREAD, TUNE; curve, variation, individuality, seed), applied in `notesGranular()` at note-on.
    - Reachable from `research-renderer`.
    - Tested: bit-identical when off, deterministic, stable per key, no allocation.
    - Delivered with A/B renders against baselines A and B and a CPU benchmark.
    - It validates the central claim (one granulator, per-key identity, full compatibility) without GUI, state or analysis work.

## Verified vs assumed

| Verified here | Assumed / not measured |
|---|---|
| The OSP architecture and seams (file:line in 01) | Mac or DAW CPU |
| Grain and Phrase code structure; lab engine tests pass | The hybrid voice's CPU |
| Per-voice cost parity between Grain and OSP granular (same VM) | Phrase on real recordings |
| Phrase analysis on synthetic fixtures (79/79 and 42/42) | Memory of OSP scenes |
| The OSP heavy-scene worst case (384 grains, 40 % / 81 % p99) | Listening quality of KEY DNA in OSP |

## Open decisions for you

1. **Natural ↔ Grain:** include it in the first unified version (Phase 6), or later? And should GRAIN MIX be a visible control in the One Shot hover strip, or route-only?
2. **Grain budget default:** unlimited (strict compatibility), or a CPU-safe ceiling that only affects extreme new settings?
3. **Phrase import:** should a dropped recording that looks like a phrase *ask* ("Play as one sound / Map its notes"), or always load as one sound with mapping in a menu?
4. **KEY DNA parameters in v1:** the five existing granular parameters only (POS, SIZE, DENS, SPREAD, TUNE), or wait for SCATTER and PAN (Phase 2) so the editor ships with seven?
5. **Atlas storage:** region copies (simple, recommended first), or invest in shared-buffer regions now?
6. **Proceed with Phase 0 + Phase 1** as the first implementation milestone?
