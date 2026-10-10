# Library Stage 0: baseline audit

The ANDOR/OSP Library is built on the existing instrument (the plugin is still `OSP`
internally). This file records the instrument as it stands at the start of the Library work,
so every later stage can be checked against it. Sample Forge is audited in
[01-sample-forge-audit.md](01-sample-forge-audit.md); the state the plugin saves today in
[02-existing-state-contract.md](02-existing-state-contract.md); risks in
[03-risk-register.md](03-risk-register.md).

## Baseline

| Item | Value |
|---|---|
| Branch | `claude/charming-lovelace-n25fte` |
| Baseline commit | `1537f7e` (meta-modulation), rollback checkpoint for the Library work |
| macOS CI | run 97 green: universal build, ctest, benchmark, AU validation (`auval`), test package |
| Local (Linux) | `ctest` 4/4 (core: unit, integration, regression goldens, smoke); plugin build `ctest` 5/5; `[ui]` 7 cases (xvfb) |
| Goldens | `tests/regression` (bit-exact engine renders), unchanged |
| Screenshots | the existing UI suites (`modui-*`, `andor-*`, `meta-*`, `eq-*`) are the visual baseline |
| DAW recall | Not tested in a DAW (none available here). Headless recall tests cover sessions, presets, starting states, portable instruments and older state versions. |

## Plugin identity (must not change)

| | |
|---|---|
| Product / company name | `OSP` / `OSP` (window title shows ANDOR/OSP) |
| Bundle ID | `com.osp.instrument` |
| Manufacturer / plugin code | `Ospx` / `Osp1` |
| Formats | AU, VST3, Standalone (macOS); VST3, Standalone elsewhere |
| State | APVTS XML, `stateVersion` 10 |

## Architecture as it touches the Library

- **Processor** `apps/plugin/Source/PluginProcessor.*`: parameters (APVTS, about 1,000),
  state (`createStateXml` / `applyStateXml`), presets, starting states, portable instruments,
  undo (`juce::UndoManager`), loads.
- **Editor** `PluginEditor.*`: the preset bar (`PresetBar` in `MainSections.*`) lists the
  factory starting states, user starting states and presets (`presetList()`), with step
  arrows and a menu. There is no browser overlay.
- **Loading** `InstrumentLoader.*`: on a background `ThreadPool`, a file is hashed (SHA-256),
  copied into the managed **SampleStore**, decoded, analysed (analysis cached as versioned JSON
  next to the audio) and built into a `LoadedInstrument` in three stages (provisional, with
  continuation, with register anchors). Multi-file drops build an `InstrumentSet`.
- **Hand-over to audio**: each layer A/B/C has a `ModelExchange` (pending / playing / retired).
  The audio thread swaps in a pending instrument and keeps retired ones alive until no voice
  uses them (`engine.isModelInUse`). Nothing is decoded, read or allocated on the audio
  thread.
- **Engine** `src/engine` (pure C++, no JUCE): voices, layers, modulation, effects. The
  baselines A/B live in `src/audio/sampler` and are not touched by the Library.
- **Analysis** `src/analysis` (pure C++): onsets, pitch, envelope, spectral features, loops.
  Useful to the Library's one-shot classifier (Stage 6) and trimming (Stage 8).
- **Formats** `src/io`: audio decoding (JUCE formats: WAV, AIFF, FLAC, Ogg, MP3 where
  available), SHA-256 (`ContentHash`), analysis JSON.
- **Dependencies** fetched by CMake (`cmake/OspDependencies.cmake`): JUCE, Catch2,
  signalsmith-stretch, plus `linear`. No database library. SQLite exists as a system library on
  macOS (SDK) and Linux (`libsqlite3`), so it can be linked without vendoring (decision D-03).
- **CI**: macOS only (`.github/workflows/ci.yml`), builds universal binaries, packages the
  `OSP-macOS-test` artifact; no signing.

## What already exists of the Library

| Spec item | Today |
|---|---|
| Managed audio storage | `SampleStore`: `<Application Support>/OSP/Samples/<sha256>.<ext>`, copied via `.partial` then renamed; analysis cache `<sha256>.analysis.json` |
| Preset | `.osppreset` = the state XML; sounds by content hash + original path (no audio inside) |
| Template | `.ospstate` "starting state" = the state XML without the layer trees (no sounds) |
| Portable preset | `.ospinstrument` zip: `manifest.json` (schemaVersion 1), `source/<sha256>.<ext>`, `analysis/`, `preset.xml` |
| Factory content | seven built-in starting states in code (Natural, Alive, ...) |
| Recent / history / search / tags / scanning / inbox / slicing / backup / packs | none |

## Stage 0 change

- **Security fix (R-01):** `.ospinstrument` import trusted entry names. A crafted
  `source/../x` wrote outside the sample store. Entries must now be plain file names in their
  folder, are written as `.partial` then renamed, and each source's bytes must match its hash
  name. Test `[plugin][security]`.
