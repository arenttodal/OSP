# ANDOR ANALOG · Migration plan to the shared FX / SATELLITE / SUB / USER MACROS (Phase 5)

**Status:** plan only. Nothing has been changed in ANALOG for this. Phase 5 shipped the isolated
D2 integration instead ([arenttodal/analog#13](https://github.com/arenttodal/analog/pull/13), into
`feature/aux-live-input`, pin `claude/analog-pin-d2` = `b522a06` + D2). It does not move ANALOG to
the OSP default line.

**Why a migration is needed:** see `analog-osp-dependency-audit.md`. ANALOG's FX, SATELLITE, SUB
and USER MACROS were promoted into OSP's shared library. ANALOG still carries its own copies with the
same file names. A pin move to the default line makes ANALOG's tests resolve OSP's headers, and the
plugin would ship two implementations of each.

## 1. What is duplicated today

The OSP side was measured on the `osp-main` checkout (`08ebd01`). The diff counts are changed
lines after normalising the namespace (`andor::analog::fx` → `osp::fx`).

| Module | ANALOG copy | Shared OSP version | Divergence |
|---|---|---|---|
| FX engine | `dsp/fx/*` (14 files) | `src/engine/fx/*` (same 14 names) | `FxProcessors.cpp` 4 lines, `Effects.h` 4, `FxEngine.h` 43, `FxGraph.cpp` 134 |
| FX UI | `product/Source/Fx{Rack,Routing,Parameters,Presets,Identity}.*`, `EqSpectrum.*` | `shared/andor-ui/Source/Fx*.*` | Not yet diffed file by file (step 1 below) |
| SATELLITE | `product/Source/SatellitePanel.*` | `shared/andor-ui/Source/SatellitePanel.*` | Same |
| SUB | `dsp/SubOsc.*`, `product/Source/SubPanel.*` | `src/engine/sub/SubOsc.*` + `SubEnvelope.h`, `shared/andor-ui/Source/AndorSubPanel.*` | Same |
| USER MACROS | `dsp/UserMacroCore.h`, `product/Source/UserMacroState.*`, `UserMacroUI.*` | `UserMacroCore.h`, `UserMacroHooks.h`, `shared/andor-ui/Source/UserMacroBank.*` | `ProductInfo::UserMacros` is now an alias of `UserMacroHooks` (API change) |

The OSP versions are the ANALOG code moved and generalised. Where they differ, the differences are the
generalisation: product hooks, OSP's parameter namespace and the shell parity rules.

## 2. Order of work

Each step is its own branch from `feature/aux-live-input`, kept green before the next. Never into
`main` without release approval.

1. **Diff inventory (no code change).** For each pair, list every behavioural difference: DSP,
   parameter IDs and ranges, defaults, state keys, drawing. Classify each as *generalisation* (take
   OSP's), *ANALOG-only behaviour* (needs a product hook) or *bug fix on one side* (port it).
   Output: one table per module.
2. **FX DSP first** (largest, least UI). Delete `dsp/fx/*` and use `osp::fx`. Keep ANALOG's FX
   parameter IDs. Gate: `FxEffectTests`, `FxGraphTests` and `MainSendTests` pass against the shared
   engine, and FX audio renders are bit-identical, or the difference is explained.
3. **FX UI** (rack, routing, presets, identity, EQ spectrum). Switch to the shared components through
   `ShellProduct` hooks. Gate: FX parity states at 0 px outside masks.
4. **SUB.** Shared `SubOsc` + `SubEnvelope`, `AndorSubPanel`. ANALOG keeps its own `sub.*`
   parameter namespace (the shared code already allows this). Gate: `SubTests`,
   `AnalogSubPanelTests`, SUB parity.
5. **SATELLITE.** Shared `SatellitePanel` / `SatelliteLive`. Gate: `SatelliteTests`, parity.
6. **USER MACROS.** Adapt `ProductInfo::UserMacros` to `UserMacroHooks`, and use `UserMacroBank` in
   place of `UserMacroUI`. The state keys stay as in state version 19, or a migration is added.
   Gate: `UserMacroCoreTests`, save/load round trip of v19 sessions.
7. **Pin move.** Point `external/osp` at the OSP default line. Re-check the parameter layout: no
   OSP-only IDs (`synth.*` imprint, pedal loop, `arp.card*`) may appear in ANALOG's layout unless
   intended. Run the state-migration tests on stored ANALOG sessions.
8. **Re-baseline parity** for the new shell parity rules (`modifiersInWell()` in parity renders,
   `trimsOnlyWithThreeCards`, `liveCapture`, `referenceSubRow`, `characterName`). Each changed
   pixel is reviewed and explained before the new references are written.

## 3. Tests to run at every step

- **ANALOG:** `andor_analog_tests`, `andor_analog_tests_visual`, `analog_dsp_tests`, `memo_lab`,
  `performance-smoke`.
- **OSP, through the submodule:** `plugin`, `osp_visual_reference`, `andor_example_tests` (+visual).
- **Shell parity:** 18 states, 0 px outside masks, or a reviewed re-baseline (step 8 only).
- **Session recall:** ANALOG sessions from state 19 and earlier open with identical parameter values.

## 4. Risks and how they are contained

| Risk | Containment |
|---|---|
| Audio changes in FX or SUB from the generalisation | Bit-compare renders per step. Any change needs an explanation and an owner listening check |
| Saved sessions lose values | Keep ANALOG's parameter IDs; add a migration in ANALOG's state loader if a key must change |
| OSP-only parameters leak into ANALOG's layout | Layout snapshot test: the ID list before vs after |
| Parity re-baseline hides a real regression | Re-baseline only in step 8, with each change listed |
| Shared changes break OSP | Every shared change keeps OSP `[design-capture]` pixel-identical and every product's visual regression at 0 px (OSP rule) |

## 5. Effort and ownership

- **Effort:** about 4–6 focused sessions, one per step 2–7, plus review.
- **Order:** step 2 is the largest. Steps 4–6 are independent of each other after step 3.
- **Ownership:** this is ANALOG product work. It needs the owner's go-ahead per step, and a release
  decision before anything reaches `main`.
- **Until then:** ANALOG stays on the isolated pin. Later asset updates (layer-ivory, mini packs, K2
  buttons) can be cherry-picked onto `claude/analog-pin-d2` the same way D2 was.
