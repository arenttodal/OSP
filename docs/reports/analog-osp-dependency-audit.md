# ANDOR ANALOG · OSP dependency audit before the D2 pin update (Phase 4)

**Question:** is moving ANALOG's `external/osp` pin to the OSP default line (with D2) the same as importing only D2?

**Answer: no.** The default line now contains ANALOG's own FX, SATELLITE, SUB and USER MACROS code, promoted into the shared library, so a straight pin move collides with ANALOG's copies. An isolated D2 integration was prepared and validated; see section 4.

## 1. Branch heads (fetched 18 Nov 2026)

| | Ref | Commit |
|---|---|---|
| ANALOG feature (baseline: 13/13 test groups, 18/18 parity) | `feature/aux-live-input` | `e3a8090` |
| Its OSP pin | `external/osp` | `b522a06`, a single commit on `claude/analog-learn-end`, on top of `4489e33` |
| OSP default | `claude/charming-lovelace-n25fte` | `56bab47` |
| D2 PR head | `claude/gui-macro-knob-d2` ([arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26)) | `3354690` = `56bab47` + one commit |

**Pin versus target:**
- **Fork point:** `4489e33` (compact bottom edge).
- **The pin's only commit (`b522a06`):** identical by patch to `7652f42` on the default line (`git cherry`), so nothing in the pin is lost.
- **The default line since the fork:** 61 commits by patch, 440 files, +50,030 / −992. Of these, 29 files changed in OSP are ones ANALOG includes directly.

## 2. What changed, by relevance to ANALOG

| Theme | Commits | Effect on ANALOG |
|---|---|---|
| **Shared FX** (M3–M7, R1–R4): ANALOG's FX graph, effects, racks, routing and mixer moved into `src/engine/fx` and `shared/andor-ui` | `418562f`, `a8453d9`, `c073d13`, `240ce87` | **Duplicate headers with ANALOG's** (`FxEngine.h`, `FxRack.h`, `FxRouting.h`, `FxParameters.h` …). Different namespaces (`osp::fx` / `osp::plugin` against `andor::analog`), but the same file names. See section 3 |
| **SATELLITE** shared | `4e81911`, `bd20852`, `b6c6af8`, `2733ef4` | Duplicate `SatellitePanel.h` / `SatelliteLive.h` |
| **SUB** shared (`AndorSubPanel.h`, `src/engine/sub/SubOsc.h`) | `1e04df6`, `b5059ac`, `7588200`, `5868478` | Duplicate `SubOsc.h`. OSP's `sub.*` stays in OSP's own parameter namespace |
| **USER MACROS** shared (`UserMacroCore.h`, `UserMacroHooks.h`) | `f639d38`, `9415f88` | `ProductInfo::UserMacros` is now an alias of the shared `UserMacroHooks`. This is an API change in `ShellProduct.h` |
| **Shell parity rules** (`ShellProcessor.h`) | several | `modifiersInWell()` now holds in parity renders as well; new hooks `trimsOnlyWithThreeCards`, `liveCapture`, `referenceSubRow` and `characterName`. These affect ANALOG's parity render |
| **OSP-only parameters** | `d7e8e3c` (SYNTH defaults, state 15), `0e22def` / `791c3cc` (imprint, v28), `3c869fc` (pedal loop, v31) | In ANDOR OSP's layout. Whether a product's layout picks any up must be checked during a migration |
| OSP-only features and UI | Pedal Loop, Note Atlas, SYNTHESIS S2.x, polyphonic lab, live capture, multi-sample, per-card arpeggiator, Learn, library/packs, the footer (LIBRARY/SETTINGS/INFO), CHARACTER → FILTER | Mostly OSP's editor and engine. Shared UI files (EngineCard, ArpSection, MacroPopups, ShapingPopups) changed too, so ANALOG's visuals can move |
| D2 (the change actually wanted) | `3354690` | The images of the two macro packs, plus tooling |

## 3. Trial: ANALOG `e3a8090` built against OSP default + D2 (`3354690`), no pin changed

| | Result |
|---|---|
| Configure | OK |
| ANALOG plugin (`ANDOR Analog`: AU/VST3/Standalone code) | **Builds and links** (775 of 776 steps) |
| `andor_analog_tests` | **Does not compile.** `AnalogProductTests.cpp` and `AnalogLearnTests.cpp` reference `andor::analog::FxRoutingView`, `fxpresets`, `SatellitePanel`, `analyseFxEqSpectrum`, `openFxRack`, `SynthRack`, `visualIdentityFor` and others. These now resolve to OSP's same-named shared headers (namespace `osp::plugin`) instead of ANALOG's |
| So | ANALOG's unit tests and both visual-parity groups (`andor_analog_tests`, `andor_analog_tests_visual`) **cannot run** on this dependency |
| Groups that do run | analog_dsp_tests: passed. plugin / osp_visual_reference / andor_example (+visual): see section 5 |

**What this shows:**
- The plugin compiles today only because ANALOG's sources include their own copies from their own folder.
- The tests pick up OSP's copies through the include path.
- The shipped plugin would carry two implementations of FX, SATELLITE, SUB and user macros (one unused).
- That is a **migration**, not an asset update.

## 4. Proposed strategy

**A. Isolated D2 integration (recommended for now):**
- OSP gets one integration branch: `b522a06` + the D2 commit (`3354690`) cherry-picked.
- It applies with no conflicts; `docs/STATUS.md` merges automatically.
- ANALOG pins that commit.
- The change to ANALOG is exactly the macro images and the asset tooling.
- **Validation:** ANALOG was built against this tree and the full suite run (minus OSP's own unit / integration / regression groups, which are unchanged since the pin); results in section 5.
- Once D2 is approved, this becomes:
  1. an OSP branch from `b522a06` with the D2 commit (pushed for ANALOG's submodule);
  2. an ANALOG integration branch from `feature/aux-live-input` with the pin bump;
  3. a PR into `feature/aux-live-input`. Not into `main`.

**B. Full migration to the shared library (separate, planned ticket):**
1. ANALOG deletes its copies of FX, SATELLITE, SUB and user macros and uses the shared ones.
2. It renames or namespaces its tests (`osp::plugin::…`).
3. It adapts `ProductInfo::UserMacros` to `UserMacroHooks`.
4. It re-checks its parameter layout (no OSP-only IDs leaking in) and its state migration.
5. It re-baselines the 18 parity states for the parity-rule changes.

This is product work for ANALOG's owner. It removes the duplication and lets ANALOG follow the OSP default line again.

## 5. Validation results (Linux, Release, 18 Nov 2026)

**Trial: ANALOG `e3a8090` against OSP `56bab47` + D2 (`3354690`), the straight pin move**
- **Build:** the plugin builds. `andor_analog_tests` does not compile (section 3).
- **Groups that run, all passed (5/5):**

| Group | Time |
|---|---|
| analog_dsp_tests | 194 s |
| plugin | 2221 s |
| osp_visual_reference | 20 s |
| andor_example_tests | 5 s |
| andor_example_tests_visual | 32 s |

- **Cannot run:** `andor_analog_tests` and `andor_analog_tests_visual`. So ANALOG's own behaviour and its 18-state parity **cannot be verified** on this dependency.

**Isolated: ANALOG `e3a8090` against OSP `b522a06` + the D2 commit only**
- **Build:** clean, 709 of 709 steps, no errors.
- **ctest: 10/10 passed.** OSP's unit, integration and regression groups were left out; they are unchanged from the pin, which passed 13/13 on 17 Nov.

| Group | Time |
|---|---|
| analog_dsp_tests | 196 s |
| plugin | 1814 s |
| osp_visual_reference | 19 s |
| memo_lab | 52 s |
| andor_example_tests | 4 s |
| andor_example_tests_visual | 32 s |
| **andor_analog_tests** | 458 s |
| **andor_analog_tests_visual** | 33 s |
| performance-smoke | 1 s |
| andor_bootstrap_tool | 0.3 s |

- **Shell parity: 0 differing pixels outside the masks in all 18 states** (`analog-osp-dependency-audit/analog_iso_d2_parity_report.md`). ANALOG and OSP draw the same D2 macro asset.
- **Captures:** `analog_iso_d2_three_layers_2x.jpg` and `analog_iso_d2_arp_open_1x.jpg`. The ANALOG editor is unchanged except for the D2 macro caps. SUB, AUX D, routing and the product-specific controls are as in the 17 Nov baseline captures.

**Conclusion:**
- **Strategy A is safe and ready:** D2 alone, all tests green, parity at 0 px.
- **Strategy B is a planned migration:** the default line, with its shared FX, SATELLITE, SUB and USER MACROS.
- **Nothing has been pushed to ANALOG.** No pin was changed on any shared branch. The isolated tree is a local worktree (`b522a06` + the cherry-pick, uncommitted).

**After D2 is approved:**
1. Push an OSP branch `claude/analog-pin-d2` (`b522a06` + D2).
2. Create an ANALOG branch from `feature/aux-live-input` with the pin bump.
3. Open a PR into the feature branch.
