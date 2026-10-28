# Validation

Measured here on Linux (GCC 13, Xvfb) unless stated otherwise. macOS results are from CI
(macos-14, Apple Silicon): run 152 on commit 5997b3c, all green.

## ANDOR OSP is unchanged

| Check | Result |
|---|---|
| Design capture (30 states, 48 component crops; `[design-capture]`) before vs after the extraction | **0 images differ** (pixel-exact). Re-run after each extraction step and on the final build. |
| Parameter schema (`[parameter-schema]`): IDs, order, version hints, names, ranges, defaults, value texts, choices | **350 parameters, identical** to the listing generated from the unmodified HEAD build (`tests/plugin/golden/osp-parameter-schema.txt`). On Apple Silicon the probed midpoint texts differ by floating-point rounding only (25 print "-0.00" for "0.00"; `space.highCut` prints 5000.0004883 for 5000.0), so value texts may differ by the sign of zero or 1 part in a million; every other field is exact (pinned by a test). |
| OSP's full suite (`plugin`, 1459 s), with reference renders, migrations and state recall | **passed** |
| Plugin identity (`Osp1`, `Ospx`, `com.osp.instrument`, "OSP") | unchanged (CMake untouched) |

## The shell and ANDOR Example

| Check | Result |
|---|---|
| ANDOR Example builds as its own plugin (VST3 and Standalone here; AU on macOS) | built (`ANDOR_Example_artefacts/Release/VST3/ANDOR Example.vst3`, `Standalone/ANDOR Example`) |
| `andor_example_tests` (6 cases; the 7th is the visual regression below) | **passed**:<br>• identity and registry, including uniqueness against every ANDOR plugin and the identity fitting the header;<br>• schema equal to OSP's;<br>• diagnostic engine: test tone without a sound, a loaded sound played by baseline A, deterministic, MUTE silences, LEVEL −12 dB within 5 %;<br>• sound replaced while rendering; a bad file fails beautifully;<br>• presets and sessions: save, reload, routes, sources, previous state; OSP's state refused;<br>• bindings: every tagged control enabled exactly when bound, Advanced popover included |
| Visual regression `andor_example_tests_visual`: 18 states, tolerance 0 | **0 differing pixels outside the masks in every state.** Inside the masks (identity, preset name, status line): 4147 px at 1×, as expected. Report: `../regression/report-andor-example.md` |
| Bootstrap tool `andor_bootstrap_tool` (7 cases, sandboxed) | **passed**: complete product, dry run writes nothing, never overwrites, refuses every clash, validates arguments, touches only its own files |
| CMake registry check | configure fails if a product's identifiers differ from its registry entry or clash with another's |

## macOS (CI run 152, commit 5997b3c)

| Check | Result |
|---|---|
| Full ctest on macos-14 (OSP's suite, `andor_example_tests`, `osp_visual_reference`, `andor_example_tests_visual`, unit, integration, regression, bootstrap tool) | **passed** |
| `auval -v aumu Osp1 Ospx` (ANDOR OSP) and `auval -v aumu Aexm Ospx` (ANDOR Example) | **passed** |
| Visual regression on macOS | **0 differing pixels outside the masks in all 18 states** |

**Open: an intermittent macOS difference.** In the run before (151, commit e46f625), with the
same rendering code, `03-three-layers@2x` showed **15 differing pixels** outside the masks on
macOS; every other state was 0. It did not recur in run 152. It does not happen on Linux,
including a build with fused multiply-adds (`-mfma -ffp-contract=fast`). The diff image could
not be fetched from this environment, so the cause is not known yet. The test now prints the
differing pixels' bounding box (reference px) and largest channel difference, so the CI log
locates it if it recurs. Until it is explained, the regression is not claimed to be
deterministic on macOS. Its tolerance stays 0 and its masks are unchanged.

**Recurred, and addressed (unified engine Phase 3).** In run 164 (commit 0c796af) the same state
showed 122 pixels, x 514..933 and y 192..448 reference px, with the largest channel difference
2. Again it was macOS only, and both renders came from the same run. Both renders took
`createComponentSnapshot`, which draws into the platform's native image, CoreGraphics on
macOS. `tests/support/VisualStates.h` now paints each state into a `SoftwareImageType` image
(JUCE's own renderer) for OSP and the shell alike. On Linux that was already the renderer, so
the pictures there are unchanged. The diff image still cannot be fetched from this environment,
so CoreGraphics as the cause is likely but not proven: the next macOS runs will tell. Tolerance
and masks are unchanged.

## Not verified here (needs a Mac, a DAW or the owner)

- Loading ANDOR Example in a DAW (auval passes; no DAW session was run).
- Licensing of the rendered artwork for any use outside this repository: the owner's decision (`../assets/manifest.json`).
