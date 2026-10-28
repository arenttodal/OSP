# Unified engine, Phase 3: the Note Atlas as an OSP instrument

Phase 3 of the roadmap (10-implementation-roadmap.md) follows the approved Phase 2 report
(commit `0c796af`) and the Phase 3 brief. A **source A, B or C can now be a Note Atlas**: one or
more recordings with many note regions, each at its original pitch, played through OSP's
existing engine as an ordinary multi-sample instrument. There is **no automatic phrase
analysis and no GUI**: atlases are prepared by hand, by fixtures or (later) by an analysis.
Baselines A and B and every established render are unchanged. **Stopped for review before
Phase 4.**

## 1. Summary

| # | Acceptance criterion | Result |
|---|---|---|
| 1 | Conversion | `atlasset::buildInstrumentSet` turns an atlas into an `InstrumentSet`: one member per included note, made from a copy of its region by OSP's own builder. §4 |
| 2 | Correct notes | Each recorded pitch plays on its own key, untransposed (measured pitch within 8 cents, the test tolerance). Fine tuning is corrected, or kept as recorded. §5, §9 |
| 3 | Variations | Round robin (never the same take twice in a row, deterministic) and velocity layers. §5 |
| 4 | FILL | Exact key, then velocity/round robin, then the nearest recorded key within the limit (ties to the lower key), else silent. An unmapped key starts no voice and steals nothing. §5 |
| 5 | Polyphony | Each key of a chord plays its own recording; 24-note clusters render clean. §9, §10 |
| 6 | One Shot | Atlas notes play like any sound, with continuation (sustain loops, release graft). |
| 7 | Granular | Each key's grains come from its own note; no grain reads a neighbour (leak below −60 dB, measured). §7 |
| 8 | KEY DNA | Acts per played key on atlas notes as on any sound (grain positions stay inside 0..1 of the note). §7 |
| 9 | A/B/C independence | Three atlases on A, B and C; each layer's mapping is its own. §7 |
| 10 | RT-safe loading and replacement | Built on the loader thread, published through the layer's `ModelExchange`. Replacing an atlas while notes sound keeps them on their own audio. No allocation in the callback (engine and plugin tests). §6 |
| 11 | Save and reload | stateVersion 14 saves the atlas (JSON), never audio. It reopens exactly, found by content hash. A session whose audio is missing opens **empty and failed**, naming what is missing. §8 |
| 12 | Legacy compatibility | Every Phase 0/1/2 golden is unchanged. Sessions saved before v14 keep the Phase 2 grain budget. Parameter IDs and the schema are unchanged. §11 |
| 13 | Regressions | Full ctest of both trees. §11 |
| 14 | CPU and memory measured | §10 |
| 15 | Future Live Capture | A capture becomes a source like any file once its audio is finalised into a stored asset: no new playback path is needed. It is tested with a `liveCapture` source that has no path. §12 |

The grain-budget hardening the brief asked for is in §3: a new `bounded` policy that never
exceeds the budget, the default for new sessions.

## 2. What changed

**Model and I/O.**
- `src/model/NoteAtlas.{h,cpp}` (pure C++, `osp_dsp`) is ported from ANDOR Lab
  (`andor-lab eeb5f39`, `src/andor/atlas`) and extended:
  - asset origins and capture ids;
  - velocity layers;
  - note origin (analysis/user) and a velocity correction;
  - `Mapping` (FILL settings);
  - stricter validation;
  - groups by key and layer.
- `src/io/NoteAtlasJson.{h,cpp}`: schema 2, with ANDOR Lab's schema 1 read and migrated. Also
  `resolveSource`, `relinkSource` and `.andoratlas` files.

**Engine.**
- `src/engine/AtlasSet.{h,cpp}` (new): atlas → `InstrumentSet` from region copies, with a
  build report (memory, time, warnings, missing sources) and memory limits.
- `src/engine/SampleSetInference`: `finaliseSampleSet` and `makeSetMember` are factored out of
  `inferSampleSet` (bit-identical output), so both kinds of set derive their figures the same way.
- `src/model/InstrumentSet.h`:
  - per-member region identity (`regionId`, `sourceAssetId`, `regionStart/End`);
  - `fill` and `fillLimitSemitones` (infinity for legacy sets);
  - `restrictsKeys()` and `groupFor (note)`.
- `src/engine/InstrumentEngine`:
  - `memberFor` uses `groupFor`;
  - an unmapped key returns before any voice is stolen;
  - `GrainBudgetPolicy` and `GrainGate` (§3).
- `src/engine/GranularSource`, `InstrumentVoice`: the gate check at grain start, and a cap of 0
  that holds the normalisation.

**Plugin and shared loader.**
- `shared/andor-ui/Source/InstrumentLoader`:
  - `AtlasLoadRequest` and `loadInstrumentAtlas`: find by hash, decode once, check, store,
    build;
  - `LoadResult.missingSources`.
- `LoadedInstrument`: `atlas` (as found, relinks kept) and `atlasSources`.
- `OspAudioProcessor`:
  - `loadAtlas`, `missingSources`, `publishEmpty`;
  - the `Atlas` state child and recall;
  - `grainBudgetPolicy`, stateVersion 14;
  - atlas sources in presets (Library hashes) and portable instruments.

**Research harness.**
- `src/research/AtlasFixtures` (the deterministic fixtures, §9).
- `UnifiedScenes::atlasScenes()` (22 scenes), `setsFor`, `atlasReport`; scenes can name an
  atlas per layer and an atlas swap.
- `RenderSession::renderLayerInstruments`.
- `RenderConfig` root `grainBudgetPolicy`.
- `research-renderer`:
  - `--unified --set atlas` (renders, costs, `atlas.md`);
  - `--atlas <file.andoratlas>` (the developer harness);
  - `--write-atlas-fixtures <dir>`.

**Tests.**
- `tests/unit/NoteAtlasTests.cpp` (8);
- `tests/integration/AtlasSetTests.cpp` (19);
- `tests/plugin/UnifiedPhase3Tests.inc` (7);
- `[phase3-demos]` regression (21 pinned scenes, `tests/regression/golden/phase3`).

**Also fixed.**
- The macOS-only intermittent visual-regression difference. Both renders now use JUCE's
  software renderer; see §11.

## 3. The grain budget: the edge case and the bounded policy

**The edge case.** Phase 2's budget shares grains max-min fairly, and every sounding stream
keeps a floor of one grain. When more streams sound than the budget has grains, the floor
alone exceeds the budget. Example: 24 granular streams on a budget of 10 play at least
24 grains at once. Grains that were already playing when a share drops also finish their
course, which can briefly overshoot. The budget was a fair share, not a bound.

**The policy** (`GrainBudgetPolicy`, `EngineSettings::grainBudgetPolicy`):

| Policy | Behaviour | Used by |
|---|---|---|
| `shared` | Phase 2's, unchanged | plugin sessions saved before state 14; the Phase 2 `budget-*` scenes (pinned) |
| `bounded` | Never more grains than the budget | new engines, new plugin sessions, research configs without the key |

How `bounded` works, without redesigning the Granulator:

1. **An engine-wide gate.** `GrainGate {live, limit}` lives in the engine and is shared by
   every voice's granular source. A source starts a grain only while `live < limit`, and
   increments `live` when it does. `live` is recounted from the voices at the start of every
   block. It never decreases within a block (grains that end are not subtracted until the
   next block), so the gate can only err on the safe side.
2. **More streams than grains.** The newest notes (by start order, then slot) get a cap of 1;
   the others get 0. A stream with cap 0 starts no grain. Its playing grains end naturally,
   and its normalisation is held, so they fade at their own level and nothing swells.
3. **Otherwise** the max-min fair shares are exactly Phase 2's. The gate only matters in the
   transient where a share has just dropped and old grains are still playing.

An unlimited budget (0) is untouched by either policy: no gate, every stream its whole pool.

**Tests** (`[budget]`):
- 24 streams on a budget of 10: `shared` exceeds 10 (the documented floor); `bounded` never
  does, block by block.
- Three dense granular atlases at budgets 64 and 6: never above the budget.
- Unlimited is bit-identical under both policies.
- Plugin: a v13 session reopens `shared`; a v14 session `bounded`. With 24 notes on three
  granular layers (72 streams, budget 64) the two sound different, and the policy is the only
  difference (switching it reproduces the other render exactly).

## 4. The atlas schema (schema 2)

A versioned description of recorded audio and its musical interpretation. The full field
list is in `docs/analysis-schema.md` ("`*.andoratlas`").

- **Sources are immutable assets**, identified by `contentHash` (`sha256:` of the stored file).
  - Each has an `origin`: `fileImport`, `library`, `liveCapture` or `bufferSnapshot`, plus a
    `captureId`.
  - It records its rate, channels (1..2) and length in frames.
  - `relativePath`, `lastKnownPath` and `managedCopy` are only hints. Nothing requires a file
    path.
- **Notes** carry:
  - an exact frame region: `sourceStart <= attackStart <= sustain.start <= sustain.end <=
    releaseStart <= sourceEnd`, plus non-destructive fades;
  - pitch: `pitchHz`, `midiPitch`, `rootMidi`, `cents`, stability, vibrato;
  - `confidence`, `voicing`, levels, `articulation`;
  - `origin` (`analysis` with a `method`/`methodVersion`, or `user`);
  - `corrections` (what the user changed: pitch, boundaries, key, group, split, merged,
    velocity), which a later re-analysis must not silently undo;
  - `included`, `assignedKey` (−1: plays at `rootMidi`), `velocityLayer` (0..15),
    round-robin `group` and `variation`, and an optional `grainRegion`.
- **Ids** are stable (`n0007`), survive edits and are never reused.
- **`mapping`** is FILL KEYBOARD: `fill` (true), `maxTransposeSemitones` (12) and
  `correctTuning` (true).
- **Validation** reports errors (empty hash, channels outside 1..2, a region out of order or
  outside its source, layer out of range, duplicate ids) and warnings. An invalid atlas builds
  nothing.
- **Version 1** (ANDOR Lab's) is migrated on read:
  - notes that name a method are `analysis`, the others `user`;
  - velocity layer 0;
  - file-import sources;
  - the default mapping.
- A newer `schemaVersion` is refused. Unknown fields are ignored.

## 5. Conversion and note selection

**Conversion** (`engine/AtlasSet`, loader thread, never the audio thread):
1. Validate. Every source an included note uses must be supplied and must match its record
   (rate, channels, length). Otherwise **nothing is built**: a set is never built from part
   of its sources.
2. Sort the included notes by (play key, velocity layer, variation, position).
3. Copy each note's `attackStart..sourceEnd`, with its own fades and a 64-frame raised-cosine
   edge at each cut. A note longer than `maxRegionSeconds` (60) is cut, with a warning. Notes
   beyond `maxTotalSeconds` (300, §10) are skipped, with a warning.
4. Analyse each copy and build a member with OSP's builder (`buildProvisional`, then
   `addContinuation`):
   - it starts exactly at the region's start;
   - its root is forced to the play key, plus its cents when `correctTuning` (so it plays in
     tune), or the play key alone (so it sounds as recorded);
   - **one playback gain** comes from the loudest region, so loudness differences between
     notes and velocity layers are kept.
5. Groups are play keys (in key order), layers are velocity layers, and takes follow the
   variation order. Roles come from those places, not inference. `finaliseSampleSet` (shared
   with inferred sets) derives the dynamics and register models.

**Selection on note-on** (`InstrumentSet::groupFor`, then the engine's `memberFor`):
1. **Exact pitch**: the key's own group.
2. **Variations**: within the group, the velocity layer that the velocity selects, then the
   round-robin take (never the previous one, deterministic from the seed).
3. **Nearest pitch**, if FILL is on: the closest recorded key.
4. **Within `maxTransposeSemitones`** (inclusive). Ties go to the lower key, deterministically.
5. **Otherwise unmapped**: the key is silent. No voice starts and none is stolen, even at
   polyphony 1.

Legacy sets (files dropped together) keep an infinite FILL limit, so every key plays as
before. The `[atlas]` test "file sample sets still play every key" pins this.

## 6. Memory ownership and safe publication

- **Ownership.** Each member owns its region copy as OSP playback buffers (`PlaybackSource`,
  transients, anchors). The set owns its members; the `LoadedInstrument` owns the set and an
  immutable copy of the atlas. Decoded source audio exists only while building; afterwards
  the members hold everything playback needs.
- **No repeated decoding.** Each source is decoded once per load, however many notes it has.
  Copies are made on the loader thread.
- **Publication.** The instrument is published through the layer's `ModelExchange`, exactly
  like any loaded sound:
  - the audio thread swaps a pointer at the start of a block;
  - voices keep the set (and member models) they started on;
  - retired instruments are freed on the message thread once no voice uses them
    (`isSetInUse`).
  The callback does no copying, file access or locking.
- **Tested:**
  - engine: replace while notes sound, with old notes finishing on their own audio;
  - plugin: six atlas replacements while a chord is held and new notes start. No allocation
    in any `processBlock`, every sample finite, retired instruments freed on the timer.
- **Shared buffers later.** Each member records `sourceAssetId` and `regionStart/End`, so a
  shared-buffer implementation (members reading one decoded source in place) can replace the
  copies without changing the model or the state. It is not built: §10 shows copies are
  affordable at the chosen limit.

## 7. Interoperability

Per note, the path is: region → root → transposition → voice → playback → KEY DNA for the
played key. All of it is the existing engine.

| Feature | With an atlas | Test |
|---|---|---|
| One Shot | members play with continuation like any sound | `[atlas]` members, polyphony, files |
| Granular | grains come from the key's own note; positions are fractions of the note | boundary leak test (One Shot and Granular, < −60 dB from a 3 kHz neighbour) |
| KEY DNA | per played key, on top of the note | grain positions in 0..1 of the note, with SCATTER and PAN on |
| SCATTER, PAN, windows | unchanged; a stereo note keeps its own stereo | stereo-wide fixture |
| Grain budget | counts atlas grains like any; `bounded` holds | §3 |
| Modulation | unchanged (routes act on the layer) | `atlas-three-abc-chord`, determinism |
| A/B/C | each layer its own atlas and mapping | three atlases together, independence |

## 8. Session state (stateVersion 14) and the asset policy

**State 14** adds, per layer, an `Atlas` child of the layer's `Instrument` tree:
- `json`: the atlas (schema 2);
- `Source` children `{contentHash, filename}`: the recordings' sample-store hashes.

It replaces the `Set` child for an atlas layer. The root gains `grainBudgetPolicy`. No audio is
stored: a 3-note session is a few kilobytes. Parameter IDs and older children are unchanged.

| Situation | Policy (implemented) |
|---|---|
| Referenced files | An imported recording is copied into the managed sample store, named by the sha256 of its bytes. The atlas's paths are hints for finding it again. |
| Library assets | The Library catalogues sounds by the same hashes. A preset's sound list now includes an atlas's sources, so the Library knows which recordings a preset needs. |
| Portable instruments | `exportInstrument` packs every atlas source from the store (manifest 2, rights checked as for any sound). Importing puts them back in the store, and the atlas recalls by hash. Tested: export, delete the original, import into an empty store, play. |
| Missing samples | Recall **empties the layer first**, then loads. If any source is missing or does not match, the load fails: the layer stays empty, its state is `failed`, and `missingSources()` names each one (`asset id: name (hash)`). A damaged atlas fails the same way. The session never plays the previous sound and never looks restored. |
| Relinking | Search order: by hash in the store, then the atlas's `relativePath` (against the atlas file), `lastKnownPath`, `managedCopy`, then search folders by name. A file with the same hash is exact. One with a different hash but the same rate, channels and length is accepted with a warning, and becomes the new fingerprint (as `io::relinkSource` does). The instrument keeps the relinked atlas, so saving again records it. |
| Future live assets | A live capture or buffer snapshot is finalised into the store (a stored file with its sha256) before any atlas refers to it. From then on it is found by hash alone, with no path (tested with a `liveCapture` source). Recording into host state is never done. |

**Migration.**
- A v13 (or older) session has no `Atlas` child and no policy: it loads unchanged, with the
  `shared` budget.
- New sessions save `bounded`.
- Saving a migrated session keeps `shared`.

All three are tested.

## 9. Fixtures and real recordings

**Deterministic fixtures** (`research/AtlasFixtures`). Synthetic recordings are generated from
fixed seeds, and their atlases are placed by hand (no analysis). `--write-atlas-fixtures <dir>`
writes each one as WAV + `.andoratlas`.

| Fixture | Content |
|---|---|
| sung-four | a sung phrase: A3, C4, E4, G4 with short gaps |
| repeated-note | three takes of D4 (round robin) |
| velocity-layers | C4: two soft and two loud takes |
| bowed-phrase | G3, B3, D4: saw + bow noise, swelling |
| silence-gaps | mono plucks with long silences |
| legato | C4 D4 E4 F4 joined, regions on adjacent frames |
| stereo-wide | A3, D4 with decorrelated left and right |
| out-of-tune | +35, −40, +12 cents |
| missing-notes | only C3, C4, C5 (FILL's job) |
| boundary-probe | a 440 Hz note right next to a 3 kHz note |
| `manyRegions (n)` | n one-second notes over 12 keys (memory and lookup benchmarks) |

**Real recordings** (local only, never committed). The user's recordings are single-pitch
takes:
- the fiddle file ("Mi Jord", Fiolin 1) is one long F4;
- the four nyckelharpa bounces are G4, A4, C5 and D#5.

So they test multi-source atlases and round robin rather than a phrase:
- a 4-source nyckelharpa atlas with 12 notes, 3 takes per pitch (21.5 MB, built in 6.0 s);
- a fiddle atlas of three F4 takes (8.7 MB, 2.3 s).

Their regions were placed by a throwaway offline script for this listening test. It is not
part of OSP, and Phase 3 has no phrase analysis.

## 10. Performance and memory

Linux VM (linux-x86_64-gcc), 48 kHz, 256-sample blocks (5.33 ms). Conventions as in Phase 2:
- mean and worst keep the fastest of 3 runs per block;
- "worst seen" is the slowest block of any run, and mostly shows the VM's scheduling.

Files:
- `bench/phase3-atlas-linux*` (scenes, costs);
- `bench/phase3-atlas-memory-linux*` (memory, build, lookup);
- `bench/phase3-baseline-reference-linux.md` (the Phase 0 scenes in the same session, for
  reference).

These are VM figures, not extrapolated to other machines.

### 10.1 Callback cost

| Scenario | Scene | Mean % | Worst block % | Worst seen µs | Voices | Grains |
|---|---|---|---|---|---|---|
| One-shot, single sample (reference) | `oneshot-chord4` (Phase 0) | 1.84 | 4.79 | 435 | 4 | 0 |
| One-shot, atlas (4 regions) | `atlas-oneshot-chord` | 1.24 | 2.66 | 508 | 4 | 0 |
| Atlas melody, recorded + filled keys | `atlas-oneshot-melody` | 1.40 | 2.62 | 2981 | 3 | 0 |
| Atlas of 10 regions | `atlas-regions-10-chord8` | 2.42 | 6.85 | 1079 | 8 | 0 |
| Atlas of 50 regions | `atlas-regions-50-chord8` | 2.38 | 6.64 | 401 | 8 | 0 |
| Three atlases (A One Shot, B Granular, C One Shot) | `atlas-three-abc-chord` | 2.29 | 7.31 | 581 | 4 | 12 |
| Granular, single sample (reference) | `granular-chord4` (Phase 0) | 1.49 | 3.85 | 2383 | 4 | 11 |
| Atlas + Granular | `atlas-granular-chord` | 1.41 | 2.93 | 2387 | 4 | 9 |
| Atlas + Granular + KEY DNA, 8 notes | `atlas-granular-keydna-chord8` | 1.73 | 3.48 | 358 | 6 | 25 |
| Heavy polyphony, One Shot | `atlas-polyphony-24` | 2.51 | 10.26 | 4329 | 24 | 0 |
| Heavy polyphony, Granular | `atlas-polyphony-24-granular` | 4.15 | 12.67 | 4636 | 24 | 63 |
| Rapid repeats (round robin) | `atlas-rapid-repeats` | 1.24 | 2.06 | 358 | 2 | 0 |
| Publication while rendering (swap every 0.25 s) | `atlas-publication-while-rendering` | 1.40 | 3.68 | 1157 | 6 | 0 |

**DSP overhead.**
- An atlas note costs what any member of a sample set costs: the voice, its continuation and
  its playback are the existing ones.
- Against the single-sample references, the atlas scenes are within run-to-run noise or
  cheaper. Atlas members are short regions; the reference vowel is long.
- 10 and 50 regions cost the same per block (2.42 vs 2.38 %): the number of regions does not
  enter the callback, only the number of sounding voices does.
- The swap scene's mean is the One Shot figure, and its worst block (3.68 %) contains swaps.
  A swap is a pointer exchange at the start of a block.

**Lookup.** `groupFor` costs 10.6–12.5 ns per call at 10, 50 and 200 regions (at most 128
groups, one linear pass), once per note-on.

### 10.2 Memory and preparation

| Atlas | Notes | Region audio | Member memory | Build (one thread) |
|---|---|---|---|---|
| sung-four | 4 | 3.2 s | 1.98 MB | 0.70 s |
| 10 regions | 10 | 10 s | 5.69 MB | 2.7 s |
| 50 regions | 50 | 50 s | 28.5 MB | 12.2 s |
| 200 regions | 200 | 200 s | 113.8 MB | 50.0 s |
| nyckelharpa, 4 sources (local) | 12 | ~35 s | 21.5 MB | 6.0 s |

- **Memory.**
  - Member memory is 0.57–0.78 MB per second of stereo 48 kHz region audio. Raw stereo
    float is 0.37 MB/s; the rest is OSP's playback buffers: interpolation padding, the
    transient copy and continuation data.
  - Peak RSS of the renderer after all 22 scenes plus the 200-region atlas was 327 MB.
  - Decoded sources are freed once built.
  - The engine object grew by 16 bytes (the gate, 127 584 bytes) and each voice by 32 bytes
    (20 880).
- **Practical limit.** The default is `maxTotalSeconds = 300`: about 170–235 MB per layer and
  about 80 s of building on this VM. Notes beyond it are skipped with a warning; a region
  longer than 60 s is cut. 600 s (the provisional value) would have been up to 470 MB and
  about 3 minutes per layer.
- **Allocation.** All of it happens on the loader thread. The callback allocates nothing:
  - engine `[atlas]` determinism test (counter on);
  - plugin swap test: 6 replacements × 16 blocks, 0 allocations.

### 10.3 Lifetime

No lifetime issue was found:
- every retired instrument is kept until no voice uses it (`isSetInUse` in the engine,
  `retired[]` in the plugin);
- it is freed on the message thread;
- the plugin swap test and the publication scene finish with every sample finite and the
  expected instrument current.

### 10.4 Apple Silicon

Not available. In CI run 164 (Phase 2's commit) the macOS job's ctest failed on the
intermittent visual difference (§11), so the cost step after it never ran. The CI step now
runs even after a failed test and includes `--set atlas` (summary and `atlas.md` printed to
the log). The next run's figures can be added here.

## 11. Compatibility and regressions

| | |
|---|---|
| Phase 0 baseline (15 scenes), Phase 1 KEY DNA (15), Phase 2 demonstrations (16) | unchanged (exact on linux-x86_64-gcc). The Phase 2 budget scenes pin `shared` |
| Plugin `[unified-baseline]`, `[reimagined-migration]`, `[phase2]` | unchanged |
| Parameter schema | unchanged (no new parameter: the budget policy is session state) |
| Legacy sample sets | unchanged: infinite FILL limit; `inferSampleSet` refactor bit-identical |
| Sessions v1–v13 | load as before; `shared` budget; no atlas |
| Full ctest, both trees | pass. Plugin tree 9/9 (plugin 1645 s, integration 867 s, unit 488 s, regression 235 s on the loaded VM); engine tree: see the commit |

New pinned goldens: `tests/regression/golden/phase3` (21 atlas scenes; the 50-region scene is a
cost scenario only).

**One existing assertion was updated.** The Phase 2 KEY DNA state test checked
`stateVersion == 13`. It now checks the current version, which is 14.

**The macOS visual regression.**
- The design system's open item (validation.md) recurred in CI run 164: 122 pixels in
  `03-three-layers@2x`, largest channel difference 2.
- Both renders in that comparison are made in the same run with `createComponentSnapshot`,
  which draws into a native image, CoreGraphics on macOS.
- The states are now painted into `SoftwareImageType` images, JUCE's own renderer, as Linux
  always did. The Linux images are unchanged, and both visual tests pass here.
- The diff image could not be fetched (the artifact host is blocked from this environment),
  so CoreGraphics as the cause is likely, not proven. The next macOS runs will show it.

## 12. Future Live Capture

The pipeline is: captured audio → finalised asset (stored file + sha256) → Note Atlas →
InstrumentSet → OSP playback.

Nothing after "finalised asset" knows where the audio came from:
- the atlas records the origin (`liveCapture` / `bufferSnapshot`) and a `captureId`;
- it needs no path;
- the loader finds it by hash.

What a recorder (not built) will have to do:
1. Write the captured buffer to the sample store as a file. This must happen off the audio
   thread: a lock-free ring buffer drained by a worker.
2. Create the source record.
3. Hand the atlas to `loadAtlas`.

A retrospective snapshot is the same, from a different buffer. The playback architecture needs
no change.

## 13. Limitations

- **No analysis.** Atlases come from hand, fixtures or an outside tool (Phase 4).
- **Build time.** Each member is analysed and gets continuation: about 0.28 s per second of
  region audio on this VM (6 s for the 12-note nyckelharpa atlas). The layer shows loading
  until then, and the previous sound keeps playing for a user load. A future speed-up: reuse
  one source analysis across its notes, or build continuation lazily.
- **Region copies** cost 0.57–0.78 MB per second of stereo 48 kHz audio. That is why the
  default limit is 300 s per atlas. Shared buffers would cut it to the raw size.
- **Per-member levelling** is not done. One gain for the atlas keeps the recorded dynamics,
  so a very uneven recording plays unevenly (by design; the NOTE MAP can offer a trim later).
- **The Samples inspector's reassignment** does not apply to atlas layers. Corrections
  belong in the atlas (Phase 5's NOTE MAP).
- **Apple Silicon** figures were not available: see §10.
- The intermittent macOS visual difference is addressed by a change of renderer in the test,
  but its root cause in CoreGraphics is not proven (§11).

## 14. Recommendations for Phase 4

1. **Phrase analysis** (ANDOR Lab's yin-viterbi port) should write schema-2 atlases with
   `origin: analysis`, keep `corrections` across re-analysis, and cache its result in the
   Library by source hash.
2. **Evaluate on real phrases first.** The user's recordings so far are single-pitch takes. A
   sung or bowed phrase with several pitches is needed for the analysis gate (R4).
3. **Speed up building** before large atlases are common: share the source analysis between
   its notes.
4. **Keep region copies** until memory measurements on real sessions say otherwise. The
   model is ready for shared buffers.
5. Confirm the **300 s** atlas limit and the **bounded** budget policy as defaults.
6. Read the Apple Silicon figures from the next CI run.

## 15. Commit and branch

Branch `claude/charming-lovelace-n25fte`; COMMIT.
