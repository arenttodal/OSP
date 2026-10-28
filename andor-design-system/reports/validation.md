# Validation

Measured here on Linux (GCC 13, Xvfb) unless stated otherwise. macOS results come from CI and
are not claimed until it has run.

## ANDOR OSP is unchanged

| Check | Result |
|---|---|
| Design capture (30 states, 48 component crops; `[design-capture]`) before vs after the extraction | **0 images differ** (pixel-exact). Re-run after each extraction step and on the final build. |
| Parameter schema (`[parameter-schema]`): IDs, order, version hints, names, ranges, defaults, value texts, choices | **350 parameters, identical** to the listing generated from the unmodified HEAD build (`tests/plugin/golden/osp-parameter-schema.txt`) |
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

## Not verified here (needs a Mac, a DAW or the owner)

- AU validation of ANDOR Example (`auval -v aumu Aexm Ospx`). It is in CI; see the CI run linked in the final report.
- Loading ANDOR Example in a DAW.
- The visual regression on macOS. It is platform-independent by design, but this measurement was on Linux; CI uploads its macOS result.
- Licensing of the rendered artwork for any use outside this repository: the owner's decision (`../assets/manifest.json`).
