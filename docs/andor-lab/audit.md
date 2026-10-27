# ANDOR Lab — Milestone 0 audit (isolation, baseline, dependencies)

_Phrase + Grain research prototypes. This file records the starting point; results of later
milestones live in the other files of `docs/andor-lab/`._

## 1. Isolation

| Question | Finding |
| --- | --- |
| Where does this session run? | A **separately cloned cloud container** (`/home/user/OSP`), not a linked worktree on the user's Mac and not the user's main OSP working tree. |
| Branch | `andor-lab` (the session is pinned to it; the preferred name `feature/andor-lab` was **not** forced — no `-B`, no resets). |
| Worktrees | `git worktree list` shows only this checkout. Creating a nested `../andor-lab` worktree inside a throw-away clone would add nothing, so none was made. |
| Remote branches | `origin/andor-lab`, `origin/claude/charming-lovelace-n25fte` (same commit), `origin/drum-plugin`. There is no `main`/production branch on the remote; nothing is merged anywhere by this work. |
| Start commit | `50103c0ccf48257f5d478efad4e346cd1c0abc49` — "CPU step 1: bit-exact grain reads, voice economy, an opaque source display" (the latest committed OSP). |

## 2. Source revision and the Drums branch

- `andor-lab` == latest OSP. All new work starts here.
- `origin/drum-plugin` (3e7796c, ANDOR Drums M2–M9) branches from e344270 (MOD-FINAL) and adds
  `apps/drums`, `src/drums` (BreakSlicer, DrumClassifier, DrumKitEngine, DrumLayers,
  DrumMapping, PadModulation) and small engine hooks. It was **not merged**: Phrase and Grain
  need pitch tracking and granular synthesis, not drum slicing or classification. Nothing from
  it is required. What *was* reused is its **pattern** for a second plugin in this repository
  (own `juce_add_plugin` target, own identity, OSP's design sources compiled again with the
  product's own `BinaryData`).
- Identities already taken (verified in both branches):

  | Product | Manufacturer code | Plugin code | Bundle id |
  | --- | --- | --- | --- |
  | ANDOR/OSP | `Ospx` | `Osp1` | `com.osp.instrument` |
  | ANDOR Drums (drum-plugin branch) | `Ospx` | `Adrm` | `com.osp.andordrums` |
  | **ANDOR Phrase (new)** | `Ospx` | `Aphr` | `com.osp.andorphrase` |
  | **ANDOR Grain (new)** | `Ospx` | `Agrn` | `com.osp.andorgrain` |

## 3. Assets actually present

| Asset | Present? |
| --- | --- |
| Rendered macro knob (Blender, "Satin Ivory") | yes: `apps/plugin/Resources/knobs/macro-ivory/macro-ivory-{176,352}.zip`, renderer `SpriteKnobRenderer` |
| Ceramic skin pack (direction 09 Warm Ceramic + Teak) | yes: `apps/plugin/Resources/skin/ceramic-skin.zip` (12.5 MB; OSP-specific layout pieces) |
| Typefaces (Inter, Outfit, SIL OFL) | yes: `apps/plugin/Resources/fonts` |
| Design tokens / drawing | `apps/plugin/Source/Design.{h,cpp}`, `OspLookAndFeel.{h,cpp}` |
| Real recordings (corpus) | **no** — the corpus is local-only by design (`docs/corpus.md`). Real-recording evaluation of Phrase is therefore a user step (instructions in `docs/andor-lab/test-instructions.md`). |

## 4. Baseline build and tests (this container)

Environment: Ubuntu 24.04, GCC 13.3, CMake 3.28, Ninja; 4 vCPU "Intel Xeon Processor @ 2.80GHz"
(virtualised), 15 GB RAM. **Linux container numbers are not representative of a Mac.**

| Step | Result |
| --- | --- |
| `cmake -B build -G Ninja && cmake --build build` | OK (234 targets) |
| `ctest --test-dir build` | **4/4 passed** (unit 437 s, integration 503 s, regression/goldens 56 s, performance-smoke 1.4 s) |
| `research-renderer --benchmark` (baseline A, 24 voices, 48 kHz, block 128) | 2.3x realtime; mean 44 % of the block budget, p99 89 %, worst 241 % (container jitter) |
| `research-renderer --benchmark --engine C --voices 16 --no-retrigger` | 5x realtime; mean 20 %, p99 40 % |
| Plugin build (`-DOSP_BUILD_PLUGIN=ON`, Linux: VST3 + Standalone) | see `docs/andor-lab/build-and-validation.md` |

Linux needed `libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
libxcomposite-dev libfontconfig1-dev` for the plugin (CI installs the same list).

The only edit to shared code before the baseline ran was a read-only accessor on
`YinPitchTracker` (the CMND curve of the last frame, for Phrase's candidate tracker);
`analyse()` is unchanged and the golden renders pass bit-exactly.

## 5. Dependency inventory

| Dependency | Version | Licence | Used by |
| --- | --- | --- | --- |
| JUCE | 8.0.9 (FetchContent) | AGPLv3 / commercial | plugins, I/O |
| Catch2 | 3.9.1 | BSL-1.0 | tests |
| Signalsmith Stretch / Linear | 1.4.0 | MIT | OSP (not used by Phrase/Grain) |
| SQLite (system) | — | public domain | OSP Library (not used by Phrase/Grain) |
| Inter, Outfit | — | SIL OFL 1.1 | GUI |

**No new third-party dependency** is added by Phrase or Grain (see the analysis-backend
evaluation in `phrase-analysis.md` for why WORLD and Basic Pitch were not integrated).

## 6. Architecture decisions (how the spec's suggested layout maps onto this repo)

The spec's `shared/ products/ tests/` tree is adapted instead of moving OSP's files (rule 7;
"avoid a massive refactor before working prototypes exist"):

| Spec | Here |
| --- | --- |
| `shared/audio/NoteAtlas` | `src/andor/atlas` (pure C++), `src/andor/io/NoteAtlasJson` (JUCE JSON) |
| `shared/audio/Analysis/*` | `src/andor/phrase/PhraseAnalysis` (reuses `analysis/pitch/YinPitchTracker`) |
| `shared/audio/Granular/*` | `src/andor/grain` (grain pool, scheduler, windows, Key DNA) |
| `shared/audio/Wavetable` | `src/andor/grain/Wavetable` (also Phrase's "Synthetic" layer) |
| `shared/audio/Effects` | OSP's `engine/SpaceReverb`, `engine/EchoDelay` reused unchanged |
| `shared/gui/*` | `apps/andor-common` (waveform, keyboard map, curve editor) over OSP's `Design`/`OspLookAndFeel` |
| `products/Phrase`, `products/Grain` | `apps/phrase`, `apps/grain` (separate `juce_add_plugin` targets) |
| `tests/{shared,phrase,grain,integration}` | `tests/andor/*` (executable `andor_tests`, ctest `andor-*`) and `tests/andor/plugin` |

The libraries are `andor_dsp` (pure C++20, no JUCE, links `osp_dsp`) and `andor_io`
(JUCE-backed). OSP's targets do not link either, so OSP cannot regress through them.
