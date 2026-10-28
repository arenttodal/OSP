# 03 — ANDOR Phrase vs OSP

Prototype: `origin/andor-lab` at `54afd07`, read-only extract. The branch changes only two
shared OSP files: `YinPitchTracker` (additive accessors `lastCmnd`, `lastFrameSilent`, lag
bounds) and nothing else of behaviour.

## 1. What Phrase is

1. **Import** on a worker: hash (SHA-256), decode with OSP's `io::loadAudioFile` (≤ 300 s, ≤ 2
   channels), copy into its own content store `ANDOR Lab/Samples`, overview
   (`apps/andor-common/AndorStore.cpp:239-289`). No resampling: the rate ratio is folded into
   the read increment.
2. **Analysis** (`src/andor/phrase/PhraseAnalysis.cpp`): high-passed mono analysis copy;
   pitch by OSP's FFT YIN in two backends — `yin` and the default `yin-viterbi` (pYIN-like:
   30 thresholds, ≤ 6 candidates per frame, Viterbi with voicing and jump costs); segmentation
   by voicing, pitch-change (≥ 0.6 st held ≥ 40 ms, vibrato veto) and re-articulation
   (level dips, tremolo veto) — **no spectral-flux onset detector**; per-note description
   (weighted median pitch, stability, vibrato, confidence); attack/sustain/release markers;
   articulation; rounding to keys with an optional auto tuning offset; repeats grouped as
   variations.
3. **Note Atlas** (`src/andor/atlas/NoteAtlas.h`, schema 1): sources by content hash (never
   embedded) + notes with frame markers `sourceStart ≤ attackStart ≤ sustain ≤ releaseStart ≤
   sourceEnd`, pitch, confidence, articulation, a `Corrections` bitset, `assignedKey`,
   variation group/index, optional `grainRegion`. JSON with 17 significant digits (exact
   round trip), refuses newer schemas, relinking by hash / compatible identity.
4. **Playback** (`PhraseEngine`): 32 voices; exact key → else nearest recorded key within the
   FILL limit (OFF 0 / NATURAL 2 / BALANCED 5 / WIDE 12 / EXTENDED 24 st) → variation
   (sequential / random / shuffle, seeded); OSP's `SincInterpolator` reads; one correlated
   loop over whole periods; 20 ms release cross-fade into the recorded release; 8-grain
   "bloom"; TONE SVF; a synthetic oscillator; `SharedFx` sends.
5. **Editing** (`PhraseEdits.cpp`): pure functions — set root, move markers, assign key,
   include, group, split, merge, re-analyse, and a re-analysis merge that never undoes user
   corrections. Edits rebuild the model on the message thread and publish lock-free; playing
   voices keep the old model until they end.

## 2. Verified evidence

Re-run in this session (`bench/phrase-eval.txt`, `bench/phrase-stress.txt`):

| Set | `yin` | `yin-viterbi` (default) |
|---|---|---|
| 13 standard fixtures, 79 notes | 79/79, F 1.00 | 79/79, F 1.00, onset 4.6 ms, pitch 0.3 c |
| 8 stress fixtures, 42 notes | 37/42, F 0.66 (false splits on noise, breath) | 42/42, F 1.00 |

Speed ≈ 40× real time. **All fixtures are generated audio** (additive tones through formant
filters); the stress set was tuned against after round 1, so it is no longer held out. **No real
recording has been evaluated** — the decisive unknown for Phrase.

## 3. Overlap with OSP

| Capability | OSP today | Phrase | Duplicate? |
|---|---|---|---|
| YIN core | `src/analysis/pitch/YinPitchTracker.cpp` | reused + accessors | no |
| Pitch aggregation | `PitchAnalyzer`: one root per file, weighted median | per-frame Viterbi + per-note median | partial (granularity differs; Phrase re-implements medians that `core/Statistics.h` has) |
| Onsets | `OnsetDetector` (spectral flux) | voicing/pitch/level segmentation | no (different method) |
| Slicing | Library Stage 8 `Slicer` (energy onsets, trim, split/merge, render slices) + `TrimSheet` region editor | pitch-aware non-destructive regions + split/merge | partial (same verbs, OSP has no pitch/root/variation) |
| "This file is several notes" | `Classifier::heldNotes` → `multipleEvents` | full note list | natural hand-off point |
| Content store | `library::storeContent` / `findStored` | `AndorStore` | **yes, exact** |
| Sinc playback, padded source, model hand-over | `SincInterpolator`, `PlaybackSource`, `ModelExchange` | reused | no |
| Sustain | `ContinuationModel` multi-loop walk + release graft (whole buffer) | single loop + release cross-fade | **yes, a simpler duplicate** |
| Multi-sample mapping | `InstrumentSet` pitch groups / velocity layers / takes; `memberFor` (unlimited transposition) | `keyRegions` + FILL limits | partial (same concept; OSP: one file per member) |
| Round-robin | seeded never-repeat | sequential/random/shuffle | yes (small) |
| Velocity layers | inference by names / loudness | gain only | Phrase lacks it |
| User corrections | Samples inspector assignments | `Corrections` + edits | partial |

## 4. Reuse verdicts

| Component | Verdict | Why |
|---|---|---|
| `NoteAtlas` + `NoteAtlasJson` | **Adapt** (move to `src/model` / `src/io`, document in `analysis-schema.md`) | pure, versioned, exact, relinkable |
| `yin-viterbi` tracker | **Adapt** into `src/analysis/pitch` as a second backend | clearly better on noise/breath; could also serve `PitchAnalyzer` and `Classifier` |
| Segmentation + note description | **Keep as a separate analysis module** (`src/analysis/phrase`), use `core/Statistics` | OSP has no note segmentation |
| `PhraseEdits` (incl. re-analysis merge) | **Reuse unchanged** | pure functions, tested |
| FILL limits | **Adapt** into `memberFor` | OSP's transposition is unlimited today |
| `PhraseEngine` (voices, loop, bloom, oscillator) | **Do not transplant** — OSP's voice, continuation and granulator are stronger | duplicates round-robin, weaker sustain, no velocity layers |
| `AndorStore` | **Rewrite on `library::storeContent`** | exact duplicate |
| `AuditionQueue` / `DisplayMailbox` | **Adapt** if OSP's own audition path lacks the equivalent | small, lock-free |
| `PhraseEditor` views | **Interaction reference** for NOTE MAP (regions, marker drag, note inspector, keyboard map) | prototype styling and layout |

Known gaps to carry over (from the lab's own `known-issues.md` and code reading): one source
per atlas only; analysis cap 180 s while load allows 300 s; edits run `analyseRange`
synchronously on the message thread; bloom grains read Hermite (aliasing on large upward
transpositions); a wasted voice steal when a key is silent under the FILL limit.
