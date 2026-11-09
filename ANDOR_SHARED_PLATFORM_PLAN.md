# ANDOR shared platform plan: from "OSP as a submodule" to `andor-core`

**Status: a plan.** Nothing in it has been executed. Do not start before ANDOR Analog 1.0 has
shipped and its first patch release is out: the migration touches every build path of both
products and buys nothing a customer can hear.

## 1. Today (measured, 9 Nov 2026)

```
arenttodal/analog                         arenttodal/OSP (one CMake project)
├── dsp/  (analog_dsp, pure C++) ─────────► src/core, audio, analysis, model, engine   (osp_dsp, pure C++)
├── product/ (ANDOR_Analog plugin) ──┐     src/io, midi/MidiFileIO, library/* (OSP_IO_SOURCES, JUCE)
│     built *inside* OSP's project   │     src/library Catalog/Preview (osp_library, SQLite)
│     via ANDOR_EXTERNAL_PRODUCTS    ├───► shared/andor-ui   (53 files: components, look, parameters,
├── tests/ (analog_dsp_tests)        │                         loader, racks; sources + BinaryData assets)
└── external/osp  = submodule ───────┘     shared/andor-shell (ShellProcessor/Editor/Engine, AndorInstrument.cmake)
      pinned at 3d949cd                    andor-design-system (registry, regression masks, HANDOFF)
      (branch claude/space-presets)        apps/plugin (OSP's own plugin)   products/andor-example
                                           tests/ (core, plugin, shell, visual), research/, tools/
```

What hurts:
- **Split lines.** Analog builds OSP's `claude/space-presets`, which is 29 commits ahead of OSP's
  default branch and 19 behind it. A dry merge conflicts in `ShellProcessor.h`, `AndorUi.cmake`,
  `PluginProcessorTests.cpp` and STATUS. Shared fixes have to be ported by hand; the arpeggiator
  allocation fix is one example.
- **Inverted build.** The product is compiled *inside* OSP's CMake project (`add_subdirectory`
  of the whole OSP tree, then `ANDOR_EXTERNAL_PRODUCTS`). Every Analog build compiles OSP's
  plugin, its tests and its research tools, and every OSP change can break Analog.
- **Shared and product code mixed.** `shared/andor-ui` holds OSP-only concepts (sampler layers,
  Note Atlas UI) beside truly shared ones (look, knobs, racks, modulation panel). `osp_dsp` holds
  OSP's sampler engine beside the shared effects (SPACE, ECHO, DRIVE, MOVEMENT), modulation,
  arpeggiator, KEY DNA and EQ/DAMPEN.
- **Frozen contracts with no owner.** The parameter schema (415 IDs), the state codecs and the
  visual parity masks are ANDOR-wide contracts, but they live in one product's repository.

## 2. Target: `arenttodal/andor-core`

One repository. It is versioned with tags (`core-1.4.0`) and consumed by products through a
**tag-pinned submodule** (or CMake `FetchContent` on a tag). It contains only code that two or
more products use.

| Package (CMake target) | Contents (moved from OSP) | Depends on |
|---|---|---|
| `andor_dsp_core` | `src/core` (FFT, pitch math, statistics, Prng), `src/audio/pitch` (sinc), `src/audio/envelopes` | — |
| `andor_fx` | `src/engine` effects: Space/Abyss/Vintage/Bloom reverbs, EchoDelay (+ASCEND), DriveProcessor, MovementBus, RhythmicShaper, CharacterFilter, LayerEq, Dampen, PostProcessor stage entry points | `andor_dsp_core` |
| `andor_modulation` | Modulation (LFOs, envelopes, routes), Arpeggiator + CardArpLanes logic, KeyDna | `andor_dsp_core` |
| `andor_analysis` | `src/analysis` (pitch, onset, envelope, spectrum, continuation), `src/model` (AnalysisData, PlaybackSource, NoteAtlas model) | `andor_dsp_core` |
| `andor_sources` | GranularSource, InstrumentVoice/InstrumentEngine's playback of a recording (the "A/B/C sources" a card plays), AtlasSet | `andor_analysis`, `andor_fx` |
| `andor_io` | `src/io` (audio files, content hash, JSON, atlas JSON), the sample store, state codecs (`keyDnaState`, curves, shaper) | JUCE core/formats |
| `andor_ui` | `shared/andor-ui` minus OSP-only views: look and feel, components, racks and faceplates, modulation panel, popovers for the shared effects, fonts and rendered assets | `andor_io`, JUCE gui |
| `andor_shell` | `shared/andor-shell` (ShellProcessor/Editor, the Engine interface, `andor_add_instrument`) | everything above |
| `andor_schema` | the ANDOR parameter schema (AndorParameters, ParameterFactory), its golden, the registry (`product-registry.json`), version-hint rules | JUCE processors |
| `andor_test_support` | ParameterSchema golden compare, render hashing, visual regression harness and masks | Catch2 |
| `cmake/` | AndorDependencies (JUCE, Catch2, signalsmith pins), warnings, `andor_check_registry`, packaging helpers (notices, installers) | — |

**Stays in OSP:**
- its sampler engine (BaselineSampler, SamplerVoice, the Phase engines specific to OSP);
- the Library (SQLite catalog);
- the OSP plugin (`apps/plugin`), research, `research-renderer` and memo-lab.

**Stays in Analog:** `dsp/`, `product/`, its goldens and tests.

### Dependency graph

```
                andor_dsp_core
            ┌────────┼──────────────┐
         andor_fx  andor_modulation andor_analysis
            └────────┼──────────────┘      │
                 andor_sources ◄───────────┘
                     │        andor_io   andor_schema
                     └──────┬───┴──────────┘
                         andor_ui
                            │
                        andor_shell
                ┌───────────┼─────────────┐
           ANDOR Analog   ANDOR OSP   future products
           (dsp/ + product/) (sampler + apps/plugin)
```

Rules:
- No arrow points upwards.
- `andor_dsp_core`, `andor_fx`, `andor_modulation`, `andor_analysis` and `andor_sources` stay
  pure C++20 without JUCE (OSP's CLAUDE.md rule).
- Products depend on andor-core, never on each other.

## 3. Migration sequence

Each step is a separate PR. Each must be **behaviour-neutral**: bit-identical renders,
byte-identical schema goldens, 0-pixel visual regressions in both products. The gates in §4
decide this.

| Step | What | Gate |
|---|---|---|
| 0 | **Reunite the OSP lines.** Merge `claude/space-presets` (+ `claude/analog-release`) and OSP's default branch into one OSP default. Resolve the four conflicts. Renumber OSP's v28 base-schema hints if Analog has shipped v29, so hints only rise. Point Analog's submodule at OSP default. | Both products' full suites green on the merged tree; Analog's schema golden updated only by appended IDs |
| 1 | **Freeze the contracts.** Copy the release schemas (OSP's and Analog's full host schema, including Analog's modulation destination list), the 1.0 state and preset fixtures and the CLASSIC render hashes per platform into `andor_test_support`. Make them read-only (CI fails on any change except appending). | New "contract" tests green on Linux, macOS and Windows |
| 2 | **Create `andor-core` as a mirror.** `git filter-repo` OSP's history for `src/{core,audio/pitch,audio/envelopes,engine,analysis,model,io}`, `shared/`, `andor-design-system/`, `cmake/` and the shared tests. This keeps history and blame. OSP keeps building from its own copies. | andor-core builds and runs its own tests in CI |
| 3 | **Split the CMake targets inside andor-core** (table in §2): move OSP-only files back out of `osp_dsp` and `shared/andor-ui`, into OSP. | Include-what-you-use clean; dependency graph checked by a CMake script (no upward links) |
| 4 | **OSP consumes andor-core** (a submodule at a tag), deletes its copies, and keeps `apps/plugin` and the sampler. | OSP: full ctest, schema golden, design capture byte-identical, render goldens identical |
| 5 | **Analog consumes andor-core directly.** Its own top-level CMake project calls `add_subdirectory(external/andor-core)` and `andor_add_instrument`; the inverted `ANDOR_EXTERNAL_PRODUCTS` build is retired. | Analog: full ctest, CLASSIC hashes, 1.0 fixtures, visual parity, auval, pluginval |
| 6 | **Release discipline.** andor-core tags follow semver: a schema append is a minor version, a schema change is forbidden, a render change is a major version with a migration. Products bump the tag in a PR that runs every contract test. | — |

Estimate: steps 0–1 take about 3–5 days. Steps 2–5 take about 1–2 weeks of build-system work,
plus a full QA cycle per product.

## 4. Compatibility tests (must exist before step 2, and run on every step)

1. **Schema contracts.**
   - OSP's and Analog's host parameter lists (ID, version hint, kind, range, default, choices,
     order) are byte-identical to the release goldens, or extend them only by appending.
   - Analog's modulation destination list is frozen.
2. **State fixtures.** Real binary state blobs and preset files saved by each released version
   (Analog 1.0, OSP's release) load with the expected parameter values, and render to the
   expected hashes (Linux) or loudness contours (macOS and Windows).
3. **Render hashes.**
   - Analog: CLASSIC `[legacy]` and `[fx baseline]`.
   - OSP: the regression goldens.
   - The engine's FX `[fx]` renders.
   - Exact hashes per platform (Linux GCC, macOS clang arm64/x86_64, Windows MSVC), recorded by
     CI for each platform rather than only on Linux.
4. **Visual parity.** OSP's design capture and every product's visual regression at 0 px outside
   their masks.
5. **Host identity.** The plugin, manufacturer and bundle codes; the VST3 class IDs (derived from
   them); the AU type; the Windows AppId. These are compared with `product-registry.json` and
   with the shipped binaries (`auval`, `moduleinfo.json`).
6. **Realtime.** No allocation in `process` (`[realtime]` suites); CPU benchmarks within 5 % of
   the previous step.
7. **Install.** CI builds both installers; the DLL dependency check (Windows); `pkgutil` and
   `codesign --verify` (macOS).

## 5. Rollback strategy

- **Every step is a PR on a branch; the previous submodule pin stays valid.** Rolling back a
  product is a one-line revert of its submodule pointer (or FetchContent tag). No product release
  is ever built from an unmerged migration branch.
- **No history rewriting in the product repositories.** andor-core is created by
  `filter-repo` as a *new* repository. OSP's history is untouched until step 4, which deletes
  files in an ordinary commit that a revert restores.
- **Dual-build window.** Between steps 2 and 4, OSP's in-tree copies and andor-core coexist. A CI
  job builds OSP both ways and compares renders and goldens. A divergence blocks step 4.
- **Release freeze.** No migration step is merged within two weeks before a product release.
  The release branch (`claude/analog-release` today) takes fixes only.
- **If step 5 fails late** (for example on a host-specific issue that shows only in the new build
  layout), Analog keeps building through `ANDOR_EXTERNAL_PRODUCTS` against andor-core's OSP-shaped
  checkout. The mechanism stays in andor-core's CMake until one Analog release has shipped on the
  new layout.

## 6. Out of scope

- Merging the products, or changing any parameter ID, plugin code, preset format or sound.
- AAX, copy protection, a shared installer. Those are product decisions, taken separately.
