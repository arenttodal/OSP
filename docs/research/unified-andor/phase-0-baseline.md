# Phase 0 — Baseline and validation harness

The proof that OSP's existing playback is unchanged by the unified-engine work. Built and
captured before KEY DNA (Phase 1) touched any engine code.

## 1. Starting point (verified)

| | |
|---|---|
| Branch | `claude/charming-lovelace-n25fte` |
| HEAD before Phase 0 | `ac7dcf4` (design system), on top of the research commits `b3d8ba9` / `db136e5` |
| Working tree | clean before the work started |
| ANDOR Lab reference | `andor-lab` at `54afd07` (read with `git archive`, never checked out). The branch has since moved to `521e051`; that commit changes Phrase files only (`git diff --stat 54afd07 521e051`), so `src/andor/grain/KeyDna.*` is identical at both. |
| Engine during capture | `src/engine` byte-identical to `ac7dcf4` (`git diff --quiet HEAD -- src/engine`) |

## 2. What the granulator does today (the parts Phase 1 touches)

| Question | Answer | Where |
|---|---|---|
| Where are a voice's grain settings made? | `InstrumentVoice::notesGranular()`. It reads the layer's live settings (`Slot::liveGranular`: the knobs, START added to POS, REVERSE, FOLLOW), then adds, in order: LIFE's position offset, modulation's START offset, the REIMAGINED mode engine's offset, the modulation routes (POS, SPREAD; SIZE and DENS ×2^octaves, clamped to the knobs' ends), KALEIDOSCOPE scatter, then LIFE's SIZE/DENS ratios, SPREAD offset and TUNE cents, and a final clamp | `InstrumentVoice.cpp` `notesGranular` |
| When is it called? | At note-on (`GranularSource::start`) and at every control period (`setParams`), also for the KALEIDOSCOPE doubling stream (`doubledGranular`) | `InstrumentVoice.cpp` |
| How are grains spawned? | Per sample countdown; interval `sr / DENS × U(0.8, 1.2)`; position `POS × frames ± (SPREAD × ½ × frames + 4 ms) × bipolar`; length SIZE; ratio `2^(TUNE/12)`; 24 grains per stream, the spawn is dropped when the pool is full | `GranularSource.cpp` `spawn`, `render` |
| Per voice or per layer? | Per voice: each voice owns its `GranularSource` (and a second for doubling). Notes are independent clouds | `InstrumentVoice.h` |
| LIFE in Granular | `InstrumentEngine::granularLife`, once per note-on, seeded `deriveSeed(seed, eventIndex, note + 0x67726c66)` | `InstrumentEngine.cpp` |

### PRNG streams (Rule 2) — what must not move

| Stream | Seed | Used for |
|---|---|---|
| Grain scheduler | `deriveSeed(shape.seed, 0x6772736d, 0)` | spawn interval jitter, grain position, display lane |
| Doubling grain stream | `deriveSeed(shape.seed, 0x6772646d, 0)` | KALEIDOSCOPE's second stream |
| Note seed | `shape.seed = deriveSeed(config.seed, eventIndex, note)` | every per-note stream |
| LIFE granular | `deriveSeed(config.seed, eventIndex, note + 0x67726c66)` | per-trigger POS/SIZE/DENS/SPREAD/TUNE |
| Reimagined continuation grains | `deriveSeed(shape.seed, 0x6772616e, 0)` | One Shot granular continuation |
| Doubling head, drift | `deriveSeed(shape.seed, 0x646f75626c65 / 0x6472696674, 0)` | |
| Performance, takes | `deriveSeed(seed, 0x706572, 0)`, `0x74616b65` … | |
| Mode engines, movement, LFO random, arpeggiator | each its own tag (`0x6d697261`, `0x6d6f73`, `0x746f79`, `0x74617065`, `0x6d6f7665`, `0x6c666f31+i`, `0x417270`) | |

None is keyed by "which key, regardless of trigger", so KEY DNA needs a new stream (Phase 1
uses a root `deriveSeed(dnaSeed, 0x6b6579646e61 "keydna", 0)` and never draws from any of the above).

## 3. The harness

| Part | What it is |
|---|---|
| `src/research/UnifiedScenes.{h,cpp}` | 15 fixed scenes (append-only): sources are deterministic test signals (the plugin reference scenes' vowel A3, pluck E2, vowel E4; no licensed audio), MIDI written out, 48 kHz, 256-sample blocks, seed 1, every engine setting explicit. Rendering, digest (exact FNV-1a sample hash, RMS, peak, brightness, stereo width), real-time cost (per-block timing as a host calls it, fastest of N runs), peak grains, peak RSS |
| `renderLayers()` (`RenderSession`) | up to three layers through engine C, each its own model, source mode, granular and layer settings, mix |
| `research-renderer --unified <dir>` | writes every scene's WAV, digest JSON, `costs.json` and `summary.md` |
| `research/configs` `"layers"`, `"mix"`, `"reimaginedRouting"` | the same layer settings for ad-hoc renders (`granular-c-layers.json`; documented in `docs/analysis-schema.md`) |
| `tests/regression/UnifiedBaselineTests.cpp` (`[unified-baseline]`, in `ctest regression`) | renders each scene twice (bit-identical), compares with `tests/regression/golden/unified/` |
| `tests/plugin/UnifiedBaselineTests.inc` (`[unified-baseline]`, in `ctest plugin`) | three sessions with non-default granular settings, made through OSP's own parameters, saved and reopened, rendered; compared with `tests/audio/unified-baseline/` |
| `src/core/PlatformTag.h` | `linux-x86_64-gcc`, `macos-arm64-clang`: stored with every digest |

**Comparison rule.** On the platform that stored a digest the samples must be bit-identical
(the exact hash). Elsewhere the hash is reported, and level (±0.05 dB), peak (±0.1 dB),
brightness (1 %) and width (2 %) must match. Apple Silicon fuses multiply-adds, so x86 and
arm64 renders differ in the last bits (the same reason the parameter-schema check needed a
rounding allowance, CI run 151).

## 4. Baseline cases

| Required | Scenes |
|---|---|
| One-shot playback | `oneshot-chord4`, `oneshot-scale-pluck` |
| Existing simple granular | `granular-scale` (defaults, LIFE 50 %) |
| Single-note granular | `granular-single` (LIFE 0: the plain grain stream) |
| Repeated notes | `granular-repeated` (same key ×6) |
| Polyphonic granular | `granular-chord4`, `granular-chord8` |
| A/B/C layered granular | `layers-abc-mixed` (A, B granular, C One Shot), `layers-abc-granular`, `layers-ab-oneshot-granular`, `stress-abc-granular-chord8` |
| Modulation-heavy granular | `granular-modulated`: LFO 1 poly → POS, LFO 2 → SIZE, ENV 1 → DENS, VEL → SPREAD, NOTE → POS, MOD WHEEL → SIZE, a meta route (LFO 2 → route 0's depth) |
| Presets with non-default granular settings | `patch-granular-textured` (POS 72, 320 ms, DENS 32, TUNE +7, SPREAD 60, START, REVERSE, FOLLOW off, LIFE 80 loose, per-layer REIMAGINED 60, SPACE), `patch-granular-glitch` (30 ms, DENS 40, TUNE −12, SPREAD 100, LIFE 90 fray); plugin sessions `plugin-granular-textured`, `plugin-granular-two-layers`, `plugin-three-layers-mixed` |
| Voice allocation | `granular-mono-glide` (mono legato with glide) |
| Existing Reimagined regression fixtures | unchanged: `tests/audio/reimagined-before/*` (18 scenes, 3 granular), `[reimagined-migration]` in the plugin suite |

## 5. Baseline numbers (this VM: 4 × Xeon 2.1 GHz, GCC 13, Release)

Full table with hashes: [`bench/phase0-baseline-linux.md`](bench/phase0-baseline-linux.md), raw:
[`bench/phase0-baseline-linux-costs.json`](bench/phase0-baseline-linux-costs.json). Mean and
worst block time as % of a 256-sample block (5.3 ms), fastest of 5 runs:

| Scene | Mean % | Worst % | Peak grains |
|---|---|---|---|
| granular-single | 0.22 | 0.64 | 3 |
| granular-chord8 | 2.12 | 6.95 | 26 |
| granular-modulated | 2.09 | 9.13 | 57 |
| layers-abc-mixed | 2.81 | 9.91 | 25 |
| stress-abc-granular-chord8 | 7.16 | 24.96 | 294 |

Memory: renderer peak RSS 26.7 MB with all scenes' models built; the `InstrumentEngine`
object is 58 744 bytes; each of its 208 voices (3 × 64 + 16 tail) is 19 392 bytes (about
4 MB, allocated in `prepare()`).

## 6. Findings

1. **OSP's output depends on the block size** (existing behaviour, not a harness artefact).
   The layers' mix, LEVEL and PAN gains glide from block to block (`InstrumentEngine.cpp`,
   "Gains glide from block to block"), so 64-, 256- and 1000-sample blocks give different
   hashes; levels agree within 0.01 dB. The baselines therefore fix the block size, and
   Phase 1 proves its own block-size independence on what it adds (each voice's profile),
   not on the whole render.
2. The engine is otherwise deterministic: every scene renders bit-identically twice.
3. Existing suites pass at the baseline: `ctest` in `build` 5/5 (unit, integration,
   regression with `[unified-baseline]`, performance-smoke, bootstrap tool).

## 7. Reproduce

```sh
cmake --build build && ctest --test-dir build -R regression
./build/apps/research-renderer/research-renderer --unified /tmp/unified-baseline   # WAVs + digests + costs
ctest --test-dir build-plugin -R plugin    # includes [unified-baseline] and [reimagined-migration]
```

The audio itself is not stored (none is, in this repository: rights and size); any checkout of
the Phase 0 commit regenerates the same WAVs, and the stored digests prove it.
