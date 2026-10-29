# Unified engine, Phase 4: phrase analysis, staged preparation, real-recording evaluation

Phase 4 follows the approved Phase 3 report (implementation `1f40764`, merged state `610db15`)
and the Phase 4 brief. **A monophonic recording - sung, bowed, plucked, played - can now be
analysed into an ordinary schema-2 Note Atlas and played as a layer's instrument**, through the
Phase 3 path (no second sampler, no second voice engine, no second granulator). Preparation is
staged: the instrument is playable at once and finished in the background. The analysis is
evaluated on real recordings, and its weaknesses are reported per recording, not averaged away.
No unified GUI, live recorder, NOTE MAP, Natural/Granular blend or installer was built.
**Stopped for review before Phase 5.**

## 1. Summary against the acceptance gate

| # | Item | Result |
|---|---|---|
| 1 | Audit and baseline | Branch at `610db15`, local tag `phase4-baseline`; both trees' ctest green before any change. §2 |
| 2 | Phrase analysis pipeline | ANDOR Lab's analysis ported (yin-viterbi + segmentation + description + edits), adapted, in `src/analysis/phrase` (pure C++). Output: an ordinary schema-2 atlas. §3 |
| 3 | Musical accuracy | Lab fixtures 79/79 and stress 53/53 notes (synthetic, not proof). Real material: §5 - pitch labels right on 77 of 78 real single-pitch takes; spliced real phrases F 0.87 (onset ±100 ms) / 0.75 (±50 ms). |
| 4 | Confidence and failure behaviour | Per-note doubts (uncertain pitch, possible octave error, uncertain boundary, unvoiced, possible split, quiet alternate), the recording's verdict and possible missed notes. Unsuitable recordings stay playable as one sample. §4 |
| 5 | Real-recording corpus and metrics | 8 spliced real phrases with exact truth, 78 real single-pitch takes against an independent tracker (pYIN), per recording. Genuine performed multi-note phrases are still missing (§5.4). |
| 6 | Preparation optimised | Profiled stage by stage. The transient separation is 2.9x faster (bit-identical). Staged build: playable after Stage A (a 6.7 s sung phrase: 0.34 s, against 1.47 s to the old finished instrument). The full build is up to 2.6x faster on one thread and 6.6x on three. §7 |
| 7 | Caching and corrections | Content hash + analysis version + settings digest cache. The re-analysis merge keeps ids, never overwrites a correction, and reports conflicts. §6 |
| 8 | Integration | Mapping, root, cents, layers, round robin, FILL, transposition limit, One Shot, Granular, KEY DNA, SCATTER/PAN/WINDOW and the budget act on analysed atlases (the Phase 3 tests plus §8). |
| 9 | Future live capture | `analysePhrase (AudioData, SourceAsset)` takes any immutable asset; `AnalysisQueue` gives bounded jobs, memory bound, cancel and progress. Nothing runs in processBlock. §6 |
| 10 | State | stateVersion 14 and schema 2 kept (fields only added). Save/reload, corrections, stable ids, missing sources, legacy presets tested. §8 |
| 11 | Capacity policy | 300 s per layer, 600 s for A+B+C together (new loads); partial builds listed and warned; the atlas keeps every note. §7.4 |
| 12 | Grain budget | Six stress tests (lowering live, dense chords with new notes, rapid stealing, A/B/C fairness, budget below the streams, sudden parameter changes); legacy `shared` policy untouched. §9 |
| 13 | Regressions | Every Phase 0-3 golden bit-identical; full ctest 9/9 (plugin tree, which includes the core suites) on the final code. §10 |
| 14 | Apple Silicon / macOS | One green macOS run (166) since the Phase 3 software-renderer fix; one run is not proof for an intermittent failure. CI now ends with the cost step so its figures are readable. §11 |
| 15 | Demos and listening | Demos A-E rendered locally (private audio, not in the repository). §12 |

## 2. Audit and baseline

- Branch `claude/charming-lovelace-n25fte` at `610db15` (Phase 3 + the merged andor-analog-shell
  PR); tagged `phase4-baseline` locally.
- Atlas schema 2 (`model/NoteAtlas`, `io/NoteAtlasJson`), `atlasset::buildInstrumentSet`,
  `InstrumentSet::groupFor` (FILL), the sample store and `ModelExchange` publication, the
  continuation builder and the realtime rules were read before changing anything.
- ANDOR Lab's code was taken from `origin/andor-lab` (the real implementation:
  `src/andor/phrase/*`, `src/andor/pitch/YinPitchTracker`), not its Phrase voice engine.
- Baselines: both trees' full ctest passed at the start (main 5/5, plugin 9/9).

## 3. The analysis

Pipeline (`analysis/phrase/PhraseAnalysis`), all deterministic:

1. **Decode / mono / high-pass** (analysis copy only; playback audio untouched).
2. **Level, silence, noise floor** (peak-relative, absolute floor, quietest-tenth floor estimate).
3. **Pitch**: yin-viterbi (pYIN-style candidates over 30 thresholds, a Viterbi path with
   transition costs and a voicing state) or plain YIN with octave folding.
4. **Segmentation**: pitch changes held long enough, level-relative re-articulation, vibrato and
   tremolo vetoes (vibrato is never a note change), DETECTION fewer/normal/more, MIN NOTE
   consolidation (scoops, falls, octave slips, weak scraps join a neighbour).
5. **Description**: stable pitch (median of the settled frames), cents, attack (walked back over
   consonants, bow noise and picks to the valley before the onset), sustain, release, fades,
   voicing, confidence, articulation.
6. **Mapping**: rounding (optional global tuning estimate), round-robin order.
7. OSP additions (analysis version 3):
   - **Harmonic repair**: a note is relabelled down an octave or more only when two things agree
     over the whole note: a spectral peak at f/2-f/5 and a YIN dip at that period four times
     deeper than at the tracked period. §5.3 explains why a frame-level rule was rejected.
   - **Quiet alternates**: a take more than 24 dB under the loudest take of its key and layer
     (a decaying tail, a breath) stays in the atlas, off the keyboard, flagged. As a round robin
     it would sound like a dropout.
   - **Doubts and the verdict**: §4.

Tried and **not kept** (each measured on the fixtures and the corpus):
- a longer attack walk for slow swells;
- a "gated file" noise-floor rule (it fixed the spliced organ phrase but broke a lab fixture, and
  helped only because the splices had digital silence that real recordings never have);
- a per-frame harmonic correction (fixed the test vowels, cost two real sung notes at joins).

## 4. Confidence and failure behaviour

Per note (`atlas::Diagnostics`, JSON `diagnostics[]`):

| Doubt | Raised when |
|---|---|
| `uncertainPitch` | confidence < 0.5; residual pitch spread (stability minus 0.7 x vibrato depth) > 40 cents; or more than 42 cents from its key. Every note of an `unsuitable` recording. |
| `possibleOctaveError` | > 20 % of its frames about an octave away, or a spectral peak under its pitch (f/2-f/5, within 35 dB and 15 dB above its surroundings) |
| `uncertainBoundary` | shorter than 80 ms, or joined legato to a neighbour within a semitone |
| `unvoiced` | voicing < 0.5 or noisy articulation |
| `possibleSplit` | joined legato to a neighbour on the same key, or a short (< 250 ms, < half the next) legato entry into a nearby note (a scoop) |
| `quietAlternate` | left off the keyboard (24 dB rule) |

Per recording (`analysis.suitability`, `pitchedSeconds`, `uncovered[]`):
- `silent`; `monophonic` (>= 60 % of the loud frames pitched); `uncertain` (>= 35 %);
  `unsuitable`.
- Pitched stretches of >= 100 ms in no note are listed as possible missed notes.

In the plugin, `silent` and `unsuitable` recordings, and recordings with no notes, are loaded
as **one ordinary sample**, playable as recorded, with the reason as the status message. No
source separation is attempted. Polyphonic mixes, noise and silence are tested (unit and
plugin).

## 5. Real-recording accuracy

Synthetic results are reported separately and are **not** evidence of real-vocal accuracy: the
lab fixtures (79/79 notes; stress set 53/53) are where the analysis was tuned.

### 5.1 Spliced real phrases (exact truth)

Public annotated datasets (vocadito, MIR-1k, ...) were unreachable from this environment
(network policy: zenodo.org, huggingface.co, archive.org blocked). So the evaluation phrases are
**spliced from the user's own real note recordings**:
- real timbre, attacks, vibrato and noise;
- artificial transitions: rests filled with each recording's own room tone, or 25 ms crossfades
  for legato;
- truth by construction: the onset is the piece's -30 dB point, the pitch is pYIN's median over
  its steady middle (independent of OSP).

The audio is private and stays local. Matching is mir_eval-like (onset within the tolerance,
pitch within 50 cents); `split` counts extra detections inside a truth note.

| Recording | Truth | Found | Matched (±50 ms) | Matched (±100 ms) | Pitch errors | Splits (±100) | Missed (±100) | Wrong notes flagged (±100) |
|---|---|---|---|---|---|---|---|---|
| bowed-legato (nyckelharpa, crossfades) | 7 | 9 | 7 | 7 | 0 | 2 | 0 | 2/2 |
| bowed-melody (nyckelharpa, rests) | 7 | 9 | 7 | 7 | 0 | 2 | 0 | 2/2 |
| organ-melody (pump organ) | 7 | 7 | 2 | 5 | 0 | 2 | 2 | 0/2 |
| plucked-melody (tagelharpa, plucks) | 7 | 7 | 7 | 7 | 0 | 0 | 0 | - |
| sung-imperfect (uneven levels, ±20 c) | 6 | 8 | 6 | 6 | 0 | 2 | 0 | 2/2 |
| sung-melody-legato | 8 | 9 | 7 | 7 | 1 | 1 | 0 | 1/2 |
| sung-melody-separated | 8 | 9 | 6 | 7 | 1 | 1 | 0 | 1/2 |
| sung-repeats-staccato (180-300 ms) | 7 | 7 | 4 | 7 | 0 | 0 | 0 | - |
| **all** | 57 | 65 | 46 (F 0.75) | 53 (F 0.87, P 0.82, R 0.93) | 2 | 10 | 2 | 8/12 |

Also at ±100 ms:
- median onset error 22 ms;
- tuning bias -2.0 cents;
- no octave errors;
- confidence of right notes 0.93, of wrong notes 0.92: **confidence alone does not separate
  them**, the doubts do (8 of 12 wrong notes flagged, 8 right notes flagged);
- analysis about 33x real time on this VM.

The 50 ms column is the usual criterion. Its extra misses are attacks placed late on slow
organ speech and on mid-sustain staccato pieces, and the ±100 ms column separates them from
segmentation errors.

### 5.2 Real single-pitch takes (78, against pYIN)

Each take is one held or plucked pitch (pYIN's 10th-90th percentile within 1.5 semitones). The
unambiguous truth is its pitch; how many regions it becomes is reported, and splits were
inspected by eye on figures (level, pYIN, OSP contour, notes).

| Family | Takes | Main key = pYIN | One region | Extra regions | Quiet tails left out | Takes with a 2nd key | Regions with a split/boundary doubt | Octave doubts |
|---|---|---|---|---|---|---|---|---|
| bowed (nyckelharpa, fiddle, sustained strings, violin) | 16 | 15 | 4 | 119 | 0 | 5 | 72 | 25 |
| plucked (plucks, tagelharpa, kalimba) | 31 | 31 | 22 | 17 | 20 | 3 | 14 | 3 |
| pump organ | 17 | 17 | 8 | 16 | 0 | 0 | 0 | 3 |
| synth / other | 5 | 5 | 5 | 0 | 0 | 0 | 0 | 0 |
| voice | 9 | 9 | 2 | 17 | 2 | 0 | 20 | 1 |
| **all** | 78 | 77 | 41 | 169 | 22 | 8 | 106 | 32 |

- **Pitch labels are reliable on real material**: 77/78.
  - The exception is a fiddle flageolet: OSP says A5, pYIN A4. The spectrum's strongest
    partial is at 877 Hz (A5), so this is disputed rather than clearly wrong.
- **Splitting is the dominant real-world issue.** By eye, the extra regions are of three kinds:
  - bow changes and re-bowing on sustained strings: real re-articulations; flagged `possibleSplit`;
  - amplitude beating on decaying plucks (tagelharpa): false splits;
  - quiet decaying tails: now left off the keyboard (22).
- Tremolo bowing (the "Trem" takes: 11-26 regions) is split into strokes. Those takes are
  mostly judged `uncertain` or `unsuitable`.
- Octave doubts on correct notes rose from 16 to 32 with the spectral rule (mostly bowed takes:
  sympathetic strings and body resonances under the note). That is the price of flagging every
  harmonic error on the test vowels below.

### 5.3 Failure examples and how a musician corrects them

| Failure | Example | Correction (data-model operations, NOTE MAP later) |
|---|---|---|
| Held note split at bow changes / beating | nyckelharpa 18.9 s → 7 regions, all flagged `possibleSplit`; tagelharpa pluck → 6 regions on beats | DETECTION "fewer" (on the 78 takes: one-region takes 41 → 48, extra regions 169 → 115, no pitch label lost), or `mergeWithNext` on the flagged joins |
| Slow-speaking attack found late | pump-organ F3/C5: the first 0.35 s of reed swell (30 dB under the phrase peak) is outside the note | `moveMarker (attackStart)` |
| Scoop labelled as its own note | sung A4 entered from G4: a 180 ms "G4" before the A4 | flagged `possibleSplit`; `mergeWithNext` (the merged note keeps the A4) |
| Harmonic lock-on | OSP's own test vowel (fundamental 30 dB under its formant): C4 read as G5 | most such notes are repaired (§3) and the rest flagged `possibleOctaveError`; `setRoot` corrects one |
| Quiet scraps as round robins | vocal fade-tail pieces 30-40 dB down | left off the keyboard automatically (`quietAlternate`); `setIncluded` brings one back |
| Not monophonic | chords, noise | verdict `unsuitable` → loaded as one sample, with the reason |

Every correction sets a `Corrections` flag and survives re-analysis (§6).

### 5.4 What is still missing

- **Genuine performed multi-note phrases** (real legato, real portamento, real ornaments)
  with reviewed annotations. The splices have real notes but artificial joins.
- Two routes:
  - allow zenodo.org in the environment's network policy (vocadito, CC BY 4.0, 40 annotated
    sung excerpts);
  - or record a few sung and bowed phrases. They would stay private: `--phrase-corpus <dir>`
    takes `<name>.wav` plus `<name>.truth.json`, and nothing is committed.

## 6. Jobs, cache, corrections, live capture

- **Entry point**: `phrase::analysePhrase (const AudioData&, const atlas::SourceAsset&,
  settings, cancel, progress)`.
  - Any immutable asset: a file import, a Library asset, a buffer snapshot, a finalised capture.
  - No file or file-picker assumption: the tests use buffers with no path.
- **`phrase::AnalysisQueue`** (pure C++):
  - bounded workers (1-8; the plugin uses 1);
  - bounded queued audio (the plugin: 600 s; beyond it a request is refused with a reason,
    never dropped silently);
  - one analysis covers at most 180 s of a recording (warned);
  - identical requests (content hash + analysis version + settings digest) share one analysis;
  - per-request cancellation: a cancelled job never reports;
  - states queued/running(progress)/finished/failed/cancelled.
- **`io::PhraseAnalysisCache`**:
  - one `.andoratlas` per key in the sample store's `phrase-analysis/` folder (a subfolder, so
    the store's `<hash>.*` lookup never mistakes it for audio);
  - read only when its contents match the key: a newer analysis version, other settings or
    other audio never read a stale entry;
  - it holds fresh analyses only, never corrections;
  - reopening a recording, or using it on another layer, reads it (plugin test: same ids,
    the caller's file name).
- **Corrections** (`phrase::edit`): set root, move markers, assign key, include/leave out
  (now a correction), group, split, merge, re-describe a region.
- **`mergeReanalysisReported`** (new; the lab's `mergeReanalysis` is now a wrapper):
  - every corrected note is kept exactly;
  - an untouched note re-detected at the same place (overlap >= 50 % of the union) and key
    keeps its id;
  - new notes get ids past every id ever used;
  - nothing is applied over a correction. Each disagreement is a conflict:
    - `analysisDisagrees`: "the new analysis finds 2 notes here (D5, E5); your note is kept";
    - `noteGone`;
    - `uncertainMatch`: an untouched note moved too far to keep its id.
  - An analysis-left-out note (quiet alternate) is re-decided; a note left out by the musician
    is a correction.
- **No analysis in processBlock**: analysis, both build stages and the cache run on the
  loader and queue threads. The plugin publishes through `ModelExchange`; the realtime test
  counts zero allocations on the audio thread while Stage B is published under a held note.

## 7. Preparation: profile and optimisation

### 7.1 Where the time went (before)

`research-renderer --prepare-bench` times each stage separately:
- decode, content hash and phrase analysis;
- then the build, split into region copies, per-region analysis, the playable model and
  continuation;
- and Stage A alone.

Linux VM, 4 cores, medians of 3 runs. Two kinds of recording:
- the spliced phrases (§5.1), 3-7 s, 7-9 notes;
- three real takes:
  - a 27.6 s bowed take with 1 region;
  - a 28.8 s fiddle take with 9 regions;
  - a 12.3 s tremolo take with 26 regions.

Before (Phase 3 code: the original median filter, one thread), milliseconds:

| Recording | Audio s | Notes | Decode | Hash | Phrase analysis | Copies | Region analysis | Playable | Continuation | Full build | MB |
|---|---|---|---|---|---|---|---|---|---|---|---|
| spliced: sung-repeats-staccato | 3.3 | 7 | 1 | 11 | 108 | 0 | 108 | 0.2 | 570 | 664 | 1.8 |
| spliced: plucked-melody | 4.7 | 7 | 2 | 16 | 160 | 1 | 183 | 0.4 | 846 | 1030 | 2.7 |
| spliced: sung-melody-legato | 5.2 | 9 | 2 | 18 | 182 | 1 | 219 | 0.5 | 1080 | 1263 | 3.3 |
| spliced: bowed-legato | 5.3 | 9 | 1 | 18 | 174 | 1 | 184 | 0.4 | 1024 | 1230 | 3.5 |
| spliced: organ-melody | 6.0 | 7 | 2 | 33 | 167 | 1 | 149 | 0.3 | 912 | 1108 | 2.7 |
| spliced: sung-imperfect | 6.2 | 8 | 2 | 22 | 195 | 1 | 178 | 0.5 | 893 | 1073 | 3.2 |
| spliced: bowed-melody | 6.3 | 9 | 2 | 22 | 207 | 1 | 182 | 0.5 | 1002 | 1188 | 3.6 |
| spliced: sung-melody-separated | 6.7 | 9 | 2 | 24 | 206 | 1 | 190 | 0.4 | 1050 | 1242 | 3.5 |
| real sung take (held D5, 6.4 s) | 6.4 | 5 | 2 | 10 | 172 | 1 | 170 | 0.5 | 867 | 1064 | 2.4 |
| real tremolo-bowed take | 12.3 | 26 | 5 | 58 | 407 | 2 | 443 | 1.0 | 2929 | 3393 | 7.9 |
| real pump-organ take | 18.7 | 1 | 7 | 91 | 612 | 4 | 719 | 2.0 | 759 | 1555 | 6.9 |
| real bowed take (nyckelharpa) | 27.6 | 1 | 11 | 98 | 769 | 5 | 1061 | 3.0 | 727 | 1798 | 10.3 |
| real fiddle take | 28.8 | 9 | 9 | 87 | 790 | 5 | 946 | 2.2 | 1851 | 2781 | 10.2 |

Findings:
- **Continuation dominated**, at about 90 ms per member regardless of its length. Inside it,
  the transient separation (an HPSS median filter over a fixed 0.53 s window per channel) took
  ~90 % of the time, and its median filters took 80 % of that.
- **Per-region analysis** costs about as much as the phrase analysis, because the same audio
  is analysed twice. Copies (< 10 ms), the playable model (< 3 ms) and finishing the set are
  negligible. There is no sample-rate conversion: regions keep the source rate and the voices
  resample as for any sound. Hashing costs about 4 ms per second of audio.

### 7.2 What was done

1. **Exact sliding median** in `TransientSeparation`:
   - the window is kept sorted and updated one element at a time;
   - the k-th order statistic is the same value, so every render is bit-identical: all
     unified, Phase 2 and Phase 3 digests are unchanged on linux-x86_64-gcc;
   - transient separation per member: 92 → 33 ms.
2. **Staged build** (`buildPlayableSet` → `enhanceSet`). Stage A is published at once: regions,
   roots, boundaries, safe fades, envelopes, levels, polyphony. Stage B adds continuation.
   - Both stages assemble the set through one function, so the finished set is bit-identical
     to the single-pass build (tested by render).
   - A note sounding when Stage B arrives keeps its model, sample for sample (tested in the
     engine and the plugin).
3. **Parallel members**: region analysis and Stage B on up to 8 threads (the plugin uses 2).
   - The result does not depend on the thread count (tested).
   - The phrase analysis itself stays single-threaded.
4. **One decode, one hash**: the analysed-recording loader decodes once for the analysis and
   both stages.

**Not done**:
- Sharing the phrase analysis with the per-region analysis: deriving the region's analysis from
  the phrase contour would change every member's character figures and the pinned Phase 3
  renders. It would make an instrument sound different from the same note loaded alone.
- Continuation for short notes (§13).

### 7.3 After

| Recording | Full build before | Full build, 1 thread | Full build, 3 threads | Stage A, 1 thread | Stage A, 3 threads | Stage A MB |
|---|---|---|---|---|---|---|
| spliced: sung-repeats-staccato | 664 | 256 (2.6x) | 101 (6.6x) | 83 | 32 | 0.9 |
| spliced: plucked-melody | 1030 | 416 (2.5x) | 169 (6.1x) | 131 | 70 | 1.4 |
| spliced: sung-melody-legato | 1263 | 607 (2.1x) | 332 (3.8x) | 167 | 85 | 1.8 |
| spliced: bowed-legato | 1230 | 512 (2.4x) | 209 (5.9x) | 173 | 72 | 1.8 |
| spliced: organ-melody | 1108 | 526 (2.1x) | 277 (4.0x) | 148 | 72 | 1.5 |
| spliced: sung-imperfect | 1073 | 597 (1.8x) | 279 (3.8x) | 173 | 80 | 1.8 |
| spliced: bowed-melody | 1188 | 550 (2.2x) | 224 (5.3x) | 181 | 73 | 1.9 |
| spliced: sung-melody-separated | 1242 | 594 (2.1x) | 315 (3.9x) | 176 | 86 | 1.9 |
| real sung take (held D5, 6.4 s) | 1064 | 856 (1.2x) | 283 (3.8x) | 168 | 72 | 1.6 |
| real tremolo-bowed take | 3393 | 2005 (1.7x) | 1026 (3.3x) | 429 | 171 | 4.3 |
| real pump-organ take | 1555 | 1286 (1.2x) | 1284 (1.2x) | 655 | 692 | 6.7 |
| real bowed take (nyckelharpa) | 1798 | 1720 (1.0x) | 1622 (1.1x) | 1098 | 1035 | 10.1 |
| real fiddle take | 2781 | 2285 (1.2x) | 1042 (2.7x) | 908 | 434 | 8.7 |

On one thread the full build is 1.7-2.6x faster where there are many short notes (continuation was the cost), and 1.1-1.2x for long takes, where the region analysis dominates. On three threads it is 2.7-6.6x faster for multi-note recordings, and 1.1-1.2x for single-region takes (nothing to share out). The audio is bit-identical in every case.

"Time to playable" (decode, hash, analysis and Stage A), against the old "time to finished"
(decode, hash, analysis and the single-pass build):

| Recording | Finished, before (1 thread) | Playable now (Stage A, 3 threads) | Finished now (3 threads) |
|---|---|---|---|
| spliced: sung-repeats-staccato | 785 ms | 133 ms | 202 ms |
| spliced: plucked-melody | 1209 ms | 244 ms | 342 ms |
| spliced: sung-melody-legato | 1465 ms | 318 ms | 565 ms |
| spliced: bowed-legato | 1423 ms | 276 ms | 413 ms |
| spliced: organ-melody | 1309 ms | 299 ms | 504 ms |
| spliced: sung-imperfect | 1292 ms | 290 ms | 489 ms |
| spliced: bowed-melody | 1419 ms | 300 ms | 451 ms |
| spliced: sung-melody-separated | 1474 ms | 343 ms | 573 ms |
| real sung take (held D5, 6.4 s) | 1248 ms | 255 ms | 467 ms |
| real tremolo-bowed take | 3863 ms | 619 ms | 1474 ms |
| real pump-organ take | 2265 ms | 1258 ms | 1851 ms |
| real bowed take (nyckelharpa) | 2675 ms | 1827 ms | 2414 ms |
| real fiddle take | 3667 ms | 1276 ms | 1884 ms |

For a spliced 6.7 s sung phrase: about 1.47 s to a finished instrument before, now playable after about 0.33 s and finished after about 0.57 s. For long single takes (19-29 s) the phrase analysis and the region analysis dominate, so Stage A gains less: 1.3-1.8 s to playable (before: 2.3-3.7 s to finished).

These are this VM's figures. Apple Silicon figures for the same benchmark will be in CI's log
and job summary from the next macOS run; no Apple Silicon latency is claimed here.

### 7.4 Capacity policy

- **Per layer**: 300 s of region audio, the provisional maximum kept from Phase 3.
- **All layers together**: 600 s for new loads (`atlasCapacityFor`).
- **Memory**: measured at 0.57-0.78 MB per region second, so a full session is 340-470 MB.
- **Live capture**: the 5-60 s target is far inside this. 300 s is the ceiling, not a default.
- **Session recall** keeps the per-layer limit only, so a Phase 3 session reopens as saved.
- **Partial builds**: beyond capacity, notes are skipped, and notes longer than 60 s are cut.
  The build is then `partial`: it lists the note ids and warns ("partial instrument: n note(s)
  left out, m cut (the atlas keeps them all)"). The atlas, its JSON in the session and the
  recording keep everything, so the instrument can be rebuilt once there is room. Never silent
  (tested).

## 8. Integration and state

**Integration.** An analysed atlas is an ordinary schema-2 atlas, so all of Phase 3 applies:
- mapping, root and cents (in tune or as recorded);
- velocity layers, round robin, FILL and its transposition limit;
- One Shot, Granular, KEY DNA, SCATTER/PAN/WINDOW, the budget, A/B/C.

New Phase 4 tests check the analysis path end to end:
- engine: a phrase analysed, built and played, each note at its key and pitch within
  0.2 semitones;
- plugin: an analysed recording plays each key.

**State.**
- stateVersion 14 and atlas schema 2 are unchanged.
- Added fields only, documented in docs/analysis-schema.md: diagnostics, suitability,
  `corrections.included`, analysis version 3.
- No new host parameters and no audio in the host state.
- Tested in the plugin:
  - save and reload of an analysed instrument with corrections (assigned key, left-out note):
    ids, flags, doubts, verdict and sound restored;
  - missing sources (Phase 3 test);
  - export with sources (Phase 3 portable-instrument test);
  - legacy presets and v13 sessions (the Phase 2/3 migration tests);
  - Library format unchanged.

## 9. Grain budget under stress

`tests/integration/GrainBudgetStressTests.cpp` (bounded policy, three granular atlases):

| Test | Result |
|---|---|
| Budget lowered 64 → 8 while grains play | never above 8 once the playing grains finish (0.25 s); no grain cut; raising it refills |
| 12-note held chord + a new note every 50 ms, budget 16 | never above 16 |
| A note-on every block, 8 voices stolen continuously | within the budget, zero allocations |
| A, B and C sharing 12 grains | each layer averages >= 2 grains and is starved in < 10 % of blocks |
| 12 streams, budget 3 | never above 3; every layer still sounds over time |
| Size, density, position, spread, SCATTER, PAN and budget jumping every 50 ms | within the budget, finite |

The `shared` policy (sessions before state 14) is untouched; its pinned Phase 2 renders are
unchanged.

## 10. Regressions

- Every golden digest is bit-identical:
  - Phase 0 unified baseline, Phase 1 KEY DNA, Phase 2 demonstrations, Phase 3 atlases;
  - OSP's own golden metrics.
- Full ctest: the plugin tree 9/9 on the final code. It includes the core unit, integration and regression suites. The main tree was 5/5 on an earlier state of this phase; its suites are the same sources.
- New tests:
  - unit: 13 phrase-analysis and edit cases ported from the lab (with an OSP content hash),
    5 diagnostics cases, 5 jobs/cache/merge cases;
  - integration: 6 staged-preparation cases, 6 grain-budget stress cases;
  - plugin: 6 cases.

## 11. Apple Silicon and the visual regression

- Run 166 (`610db15`) passed on macOS 14 (arm64 runner). It was the first run after the
  software-renderer snapshot fix; runs 164 and 165, before it, failed in
  `andor_example_tests_visual` (122 and 325 px, max channel difference 2).
- **One green run does not establish that an intermittent difference is gone.** Runs 151 and
  164 were also intermittent. It should be judged over the next several macOS runs.
- The Apple Silicon cost figures could not be read from run 166: the log tool returns only
  the tail, and AU validation followed the cost step.
- CI now runs the cost step last, adds the Phase 4 analysis accuracy and the preparation
  benchmark (1 and 3 threads), and writes everything to the job summary too.

## 12. Demonstrations (local, not committed)

All rendered by `research-renderer` from atlases analysed from the spliced phrases (§5.1).
Delivered as a local listening page; the audio is the user's and stays private.

- **A**: a sung phrase → instrument → the harness melody, against baseline A (the whole phrase
  as one transposed sample).
- **B**: a bowed (nyckelharpa) phrase → melody, and a held note.
- **C**: repeated sung takes → round robin (repetition fixture).
- **D**: Granular + KEY DNA organic on the bowed phrase (chords) and the sung phrase (melody).
- **E**: Stage A against the finished instrument.
  - Melody: difference -55.6 dBFS RMS.
  - Held note: -123 dBFS.
  - The difference is small because almost none of these short notes can loop (§13).
  - Preparation times: §7.

## 13. Limitations

1. **Short notes do not sustain.** Continuation needs a 0.5 s stable stretch
   (`ContinuationOptions::minRegionSeconds`), and 16 of 18 notes analysed from the demo
   phrases are shorter. In One Shot a held key ends when its recorded note ends. Granular
   sustains.
2. **Over-splitting of held real notes**: bow changes, beating and tremolo (§5.2). It is
   flagged, and DETECTION "fewer" halves it, but it is not solved.
3. **Slow-speaking attacks** are found late (organ swell).
4. **Weak fundamentals**: most harmonic lock-ons are now repaired, and the rest are flagged.
   The flag also fires on some correct bowed notes.
5. **Confidence is not calibrated**: right and wrong notes have the same mean confidence.
   Use the doubts, not the number.
6. **No genuine performed phrases** in the evaluation yet (§5.4).
7. **Per-region analysis is repeated** after the phrase analysis (§7.2, deliberately).
8. **Apple Silicon**: no preparation figures yet; the visual fix is unconfirmed beyond one run.

## 14. Recommendation for Phase 5

Before the unified GUI:

1. **Evaluate on genuine phrases** (§5.4) and re-run this report's tables. Nothing below should
   be tuned on splices alone.
2. **Sustain for short atlas notes**, as a member-level option for atlas sets only, so legacy
   sounds are unchanged:
   - a shorter-loop continuation for notes of 0.15-0.5 s;
   - or a granular sustain tail grafted after the region.

   This is what most decides whether an analysed phrase plays like an instrument.
3. **Re-articulation**: a bow-change/beating discriminator, evaluated on the real takes
   (§5.2). The over-splitting is the most visible remaining analysis defect.
4. **Then the unified GUI's NOTE MAP**, built on the doubts, the corrections and the conflict
   reports, which are all already in the data model.

The live recorder should wait until (2) exists: captured phrases will be short.
