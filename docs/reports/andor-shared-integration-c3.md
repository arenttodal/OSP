# Package C · Shared integration (17 Nov 2026)

Captures and the parity report are in `andor-shared-integration-c3/`.

## 1. Pull request: rendered macro knob D2

| | |
|---|---|
| PR | [arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26): open, not merged, awaiting review |
| Branch | `claude/gui-macro-knob-d2` at `3354690` (one commit), created from the OSP default branch |
| Base | `claude/charming-lovelace-n25fte` at `56bab47` (the default branch on 17 Nov) |
| Size | 6 files, +69 / −3: two packs, `postprocess.py`, `macro-knob.json`, `studio-soft.json` (adds a preset), one STATUS entry |
| CI | `build-test (macos-15, plugin ON)`: **passed** (18:58–19:58). The other, cancelled run was the first push's duplicate |
| Local (Linux, Release) | `plugin`, `osp_visual_reference`, `andor_example_tests`, `andor_example_tests_visual` pass. `[sprite-knob]`: 2053 assertions. `verify.py`: 0.21° max pointer error |

**Unchanged:**
- `SpriteKnobRenderer`, `SpriteKnobLibrary`, 129 frames, frame sizes and the manifest format;
- DSP, parameters and state.

**Before shipping, still manual** (checklist in the PR):
- slow drag, fast drag and automation playback on a real **Retina Mac**;
- the same **inside a DAW** (at least Logic and Ableton Live), with editor resize and several instances.

The marketing branch was not merged. Only the asset files and the tooling they need were moved.

## 2. ANDOR ANALOG `feature/aux-live-input`, built and captured

| | |
|---|---|
| ANALOG commit | `e3a8090` (PR #12 merge, 17 Nov), identical to `origin/feature/aux-live-input` |
| OSP pin | `b522a06` (`external/osp`). The submodule clone failed in this container, so the build used a local OSP worktree at exactly `b522a06` (`-DANALOG_OSP_DIR`) |
| Build | Linux, Release, `-DOSP_BUILD_PLUGIN=ON`. Plugin, standalone, all test binaries |

### Tests: 13 / 13 passed (ctest, 4008 s)

| Test | Result | Time |
|---|---|---|
| analog_dsp_tests | Passed | |
| plugin | Passed | |
| osp_visual_reference | Passed | 16.7 s |
| memo_lab | Passed | 46.1 s |
| andor_example_tests | Passed | 3.8 s |
| andor_example_tests_visual | Passed | 28.5 s |
| andor_analog_tests | Passed | 409.4 s |
| andor_analog_tests_visual | Passed | 28.2 s |
| unit | Passed | 509.7 s |
| integration | Passed | 843.0 s |
| regression | Passed | 240.5 s |
| performance-smoke | Passed | 0.9 s |
| andor_bootstrap_tool | Passed | 0.1 s |

**Shell parity** (`analog_visual_parity_report.md`): ANALOG against OSP at tolerance 0 is **0 differing pixels outside the masks in all 18 states**. The states cover:
- empty and 1 to 3 layers, at 2×;
- windows of 869 and 1810 px;
- all six MOD tabs;
- card EQ, DAMPEN and GRANULAR;
- keys held;
- ARP open.

The only masks are identity, preset name and keyboard actions.

### What the captures show (`analog_*.jpg`, ANALOG's own finish)

- **Compact layout:**
  - 1448 × 1050;
  - no footer status line; messages appear as a brief notice over the keybed (`analog_three_layers_2x.jpg`: "HYBRID takes one recording…");
  - the cards are shortened, with the display keys on the sample card's glass.
- **Bottom right:** the ARP and SUB block, plus **D** (AUX D), SETTINGS and INFO keys.
- **PITCH / MOD:** touch strips.
- **ARP panel:**
  - opens between the macros and the keybed;
  - the window does not change width (`analog_arp_open_1x.jpg`).
- **Macros:** the current shipping sprite (A). ANALOG gets D2 only when its pin moves past PR #26.

### The footer commits

- The superseded trimmed-footer commits from the marketing branch were **not** integrated anywhere. They are `dc75a09`, `26d1389`, `ec63f01`, `1daa605`, `9933db1` and `ef5c0c3`.
- ANALOG's compact edge is OSP's own `4489e33`, reached through its pin.
- Nothing was pushed to ANALOG, and nothing was merged into ANALOG `main`.

## 3. Revisions

| Repository | Ref | Commit |
|---|---|---|
| OSP default | `claude/charming-lovelace-n25fte` | `56bab47` |
| OSP D2 PR | `claude/gui-macro-knob-d2` | `3354690` |
| OSP marketing / assets | `claude/friendly-pascal-0y48r0` | this commit |
| ANALOG feature | `feature/aux-live-input` | `e3a8090` → OSP `b522a06` |
| ANALOG main | `main` | `4bc6c41` → OSP `adf8ed6` (9 Nov) |

**Correction to the earlier audit:**
- **What the audit said:** that `b522a06` is on the OSP default branch's history.
- **What is true:**
  - `b522a06` sits only on `origin/claude/analog-learn-end`. It is one commit on top of `4489e33`.
  - The same change reached the default branch as `7652f42` (via PR #24), and `git cherry` confirms the patches are identical.
  - The default branch is 86 commits ahead of `b522a06`'s fork point.
- **Consequence:** moving ANALOG's pin to the default line loses nothing.

## 4. ANALOG dependency update (prepared, not done)

**When:** after PR #26 is approved and merged.

1. **On an ANALOG branch from `feature/aux-live-input`:** move `external/osp` from `b522a06` to the merge commit of #26 on the default line.
2. **Rebuild, then run the full ctest.** In particular, the two visual tests: ANALOG's parity is measured against OSP's own render, so the new macro sprite appears on both sides.
3. **Recapture** the shipped states for review.
4. **Open a small PR into `feature/aux-live-input`.** Do not open it into `main`: merging into ANALOG `main` stays a separate release decision.

**Expected:**
- the D2 macros;
- whatever else the default line gained in its 86 commits since the fork point. Those commits need their own check: they are not reviewed here.

## 5. Blockers and open decisions

1. **Review and merge PR #26.** The owner decides.
2. **Manual Retina Mac and DAW interaction test** before shipping D2.
3. **ANALOG `main`** is still on the 9 Nov pin until `feature/aux-live-input` is released. That needs separate release approval.
4. **`layer-ivory` B and the minis are review-only.** They are wired locally, not committed to any shipping branch, and the 150 mini call sites are untouched (see Package B).
5. **Ceramic skin resize retention** (192 MB / 160-image LRU): a suggested separate ticket. It is not caused by the sprites.
