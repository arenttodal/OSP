# Unified engine, Phase 4B: segmentation of held and repeated notes, 0.3 s sustain

Phase 4B follows the Phase 4 report (implementation `451005f`) and the revised Phase 4B brief.
It does two things before the Phase 5 GUI:

1. **Note segmentation.** A bow change, a swell or beating no longer becomes a new note by
   default. A genuine re-attack (a re-bow, a re-pluck, a new breath) still does. Where level
   alone cannot tell the two apart, the note stays whole and says where a split might belong
   (`possibleRepeat`, with positions).
2. **Sustain.** An atlas note sustains from 0.3 s of stable material (was 0.5 s). Sample sets
   and every region of 0.5 s or more are analysed exactly as before.

The detector was improved in place. The pipeline, the Note Atlas format, ids, corrections and
caching are unchanged; only fields were added. No new datasets were built: the real-material
figures below are the Phase 4 corpora, which are small. Anything that needs musical judgement
is listed in §6 for your Phase 5 playing tests.

**Stopped for review before Phase 5.**

## 1. Summary against the completion criteria

| # | Criterion | Result |
|---|---|---|
| 1 | 0.3 s threshold implemented and tested | Atlas notes only (`atlasset::BuildOptions::minSustainSeconds`, 0.3). Tested on real vocal and bowed notes of 0.25-1.0 s and in `AtlasShortSustainTests`. §3 |
| 2 | Segmentation meaningfully improved, especially bowed and repeated notes | Real single-pitch takes: 53 of 78 found as one note (was 41); extra regions 169 → 87 (bowed 119 → 56). Spliced real phrases: false splits 10 → 4, nothing lost. §2.3 |
| 3 | Genuine re-articulations preserved | Re-bows and re-plucks with a sharp return still split (new `repeated-attacks` fixture 4/4; the spliced bowed and plucked repeats as before, plus one more re-pluck). Slow-dip repeats are the cost: kept whole and offered as splits (§2.4). |
| 4 | Uncertain detections identifiable | New per-note doubt `possibleRepeat` with `repeatAt[]` (where to split). On the spliced repeats, 2 of the 7 missed notes are offered at the exact join. §2.2 |
| 5 | Existing audio regressions pass | Every Phase 0-2 and legacy golden unchanged. The Phase 3 atlas goldens with 0.3-0.5 s regions changed - the intended sustain change, proved by reverting only the threshold (§4). |
| 6 | Preparation speed comparable to Phase 4 | Analysis +1 %, Stage A +1 %, full build +5 % (13 recordings, within run-to-run spread). §5 |
| 7 | No real-time safety regression | The render path is unchanged; shorter loops are data. A held short note allocates nothing (tested). Analysis stays off the audio thread. §4 |
| 8 | Before/after analysis results and renders | §2.3, §3; renders local only (§7). |
| 9 | Limitations documented | §6 |

## 2. Segmentation

### 2.1 What was splitting held notes

The Phase 4 detector split a note at any level dip of 9 dB against the level either side
(4.5 dB if a frame near it was unvoiced). Measured on the real takes and the spliced repeats
(depth, rise and fall times, steepest rise, voicing, pitch either side of every dip):

- **Bow changes, swells, beating** dip 6-15 dB, as deep as many real re-attacks. Their level
  comes back slowly: at most about 5 dB per 10 ms on the smoothed level, and at most 9 dB per
  10 ms unsmoothed.
- **Re-bows and re-plucks** come back fast: 7-12 dB per 10 ms smoothed, 10-17 dB per 10 ms
  unsmoothed.
- **Shallow voicing gaps** (4.5-9 dB) were almost all tremolo, harmonics, bow noise or a
  breathy tone, not new notes. Every genuine re-attack with a voicing gap was 13 dB or more.
- **Beating** is a very regular train of dips (every 0.43 s on the plucked takes) that never
  settles: under 40 % of the time within 1.5 dB of its peak. Repeated notes in a steady tempo
  are regular too, but hold their level between the dips (53-75 %).
- **Octave flips.** On bowed harmonics and low strings the tracker followed a harmonic for a
  while. The pieces were split at the jump and then described back on the same key.

### 2.2 What changed (analysis version 4)

All of it is in `rearticulations()` and `segment()`/`analysePhrase()` in
`analysis/phrase/PhraseAnalysis.cpp`:

- **A dip splits a note only as a re-attack:**
  - a sharp rise (6.5 dB per 10 ms smoothed, or 9.1 unsmoothed for a full-depth dip) at half
    the re-articulation depth;
  - a voicing gap at the full depth (9 dB);
  - or any dip 20 dB deep.
- **A re-pluck without a dip** - the level jumps 9.75 dB within 10 ms and stays 4.5 dB up -
  also splits.
- **Ambiguous dips stay whole and are flagged.** A slow dip of 9-20 dB does not split. The note
  gets `possibleRepeat` and the dip's position in `repeatAt`.
- **Beating is left alone:** three or more full-depth dips, regular (0.25-0.8 s apart) and never
  settling, are neither split nor flagged. Only a sharp attack inside the train splits.
- **Pieces on one key are joined.** Two joined pieces that were cut where the pitch moved (or
  across a 5 ms voicing gap only the pitch jump kept open), or that the harmonic repair put back
  on the same pitch, are one note. A join of a small move (not a harmonic jump) is offered as a
  split.
- **DETECTION:**
  - MORE splits at every dip the Phase 4 rule found, at softer attacks, and joins nothing on
    pitch alone;
  - FEWER asks for sharper attacks and deeper dips (×1.3-1.5);
  - NORMAL is in between.
- **`analysisVersion` 3 → 4.** The cache key includes it, so cached analyses are redone. The
  settings digest also records the new depths.

`research-renderer --phrase-corpus` now accepts `--detection`, and reports missed notes that a
note offers as a split (`missedFlagged`; JSON and the Markdown column "Missed (offered as a
split)").

### 2.3 Before / after

Phase 4 (`451005f`) and Phase 4B built side by side; same inputs, yin-viterbi, DETECTION normal
unless stated.

**Real single-pitch takes** (78 of your recordings whose independent pYIN pitch range is under
1.5 semitones: one held or plucked note each, so the truth is one note):

| Family | Takes | One note, before → after | Extra regions, before → after | Flagged `possibleRepeat` (positions) |
|---|---|---|---|---|
| bowed (nyckelharpa, fiddle, strings) | 16 | 4 → 7 | 119 → 56 | 14 (21) |
| pump organ | 17 | 8 → 8 | 16 → 16 | 0 |
| plucked (tagelharpa, plucks) | 31 | 22 → 26 | 17 → 10 | 7 (9) |
| voice | 9 | 2 → 7 | 17 → 5 | 6 (11) |
| other (synth) | 5 | 5 → 5 | 0 → 0 | 0 |
| **all** | **78** | **41 → 53** | **169 → 87** | 27 (41) |

DETECTION on the same takes (extra regions / takes as one note):

| | FEWER | NORMAL | MORE |
|---|---|---|---|
| Phase 4 | 115 / 48 | 169 / 41 | 248 / 36 |
| Phase 4B | 84 / 53 | 87 / 53 | 239 / 40 |

FEWER and NORMAL are now close on these takes: the remaining extra regions are mostly gaps and
octave flips, which DETECTION does not govern (§6).

**Real multi-note takes** (5 takes, no exact truth): 34 → 26 regions. Of the 8 fewer:

- 3 are small pitch moves on one sung key, now joined and offered as splits;
- 5 are a bowed harmonic tremolo that had been cut at every stroke (now 6 notes, flagged).

The other three takes are unchanged.

**Spliced real phrases** (Phase 4 corpus 3: 8 phrases, 57 notes, exact truth, ±100 ms):

| | Matched | False splits | Missed | P | R | F |
|---|---|---|---|---|---|---|
| Phase 4 | 53 | 10 | 2 | 0.82 | 0.93 | 0.87 |
| Phase 4B | 53 | 4 | 2 | 0.90 | 0.93 | 0.91 |
| Phase 4B, MORE | 53 | 10 | 2 | 0.82 | 0.93 | 0.87 |

Both bowed phrases are now exact (7/7, was 9 found for 7). So is the imperfect sung phrase
(6/6, was 8).

**Spliced real repeats** (corpus R: 5 files, 25 notes; the same pitch played again from its own
recorded attack, joined by 25 ms crossfades or 60 ms gaps):

| | Matched | False splits | Missed | Missed but offered as a split |
|---|---|---|---|---|
| Phase 4 | 20 | 1 | 5 | - |
| Phase 4B | 18 | 0 | 7 | 2 |
| Phase 4B, MORE | 21 | 4 | 4 | 0 |

Per file:

- **Bowed:** as before (4/5). The A4 repeat has no level feature at all.
- **Plucked:** 4/5 (was 3/5): one more re-pluck, which does not dip first, is now found by its
  step.
- **Detached sung:** 5/5.
- **Organ:** 3/5 (was 5/5). Its re-pressed legato keys dip softly, exactly like a bow change;
  both joins are offered as splits within 35 ms.
- **Legato sung:** 2/5 (was 3/5 with one false split). Its re-attacks are invisible in level.
  The extra Phase 4 match was a dip inside the source note that happened to fall within 100 ms
  of a join.

**Lab fixtures** (synthetic):

- Stress set: 53/53, unchanged.
- Standard set: 76/79 + 3 offered as splits. The legato half of `repeated` joins its notes with
  slow 12 dB dips, which now stay one note with three offered splits, each within 30 ms of the
  join; at MORE they split into 8 as before. The other 12 fixtures are unchanged.
- Two fixtures added (the existing ones are untouched):
  - `repeated-attacks`: the same joins rising back within 8 ms, 4/4;
  - `beating`: one held note with a 14 dB, 2.3 Hz swing, one note, not flagged.

### 2.4 The trade

Level alone cannot tell a slow re-articulation from a bow change. The default now favours
whole notes and says where it was unsure. What gets lost by default:

- soft legato repeats: the organ re-presses, the lab's slow-dip repeats;
- repeats with no level change at all: legato sung. Nothing level-based finds these, before or
  after.

The flags recover the first group; MORE recovers both. Not done: merging everything. Real
re-attacks still split, and the spliced phrases lost no notes.

### 2.5 Corrections, identities, cache, format

- **Format:** the Note Atlas stays schema 2. `possibleRepeat` and `repeatAt` are added fields
  (docs/analysis-schema.md); older readers ignore them.
- **Edits:** a split or merge by hand re-describes the note and drops its doubts, as before.
- **Re-analysis:** a re-analysis of an edited atlas goes through `mergeReanalysisReported`
  unchanged: ids kept, corrections never overwritten, conflicts reported.
- **Cache:** version-3 cache entries are simply not found (the key changed). They are never
  read as version 4.

## 3. Sustain from 0.3 s

### 3.1 Change

- `atlasset::BuildOptions::minSustainSeconds` (0.3) sets each atlas member's minimum stable
  region.
- Below the legacy 0.5 s, the continuation analysis scales the loop and segment minimums to
  the region: 0.6 and 0.4 of it (were 0.35 s and 0.25 s fixed, which a 0.3 s region cannot
  hold).
- Below 0.5 s, the decay limits allow the same total level change as a 0.5 s region did, so a
  loop's level step stays the same.
- `ContinuationOptions::shortRegionSeconds` defaults to 0 (off). Sample sets, legacy playback
  and every region of 0.5 s or more are analysed exactly as before; the test pins the jumps.
- No new sustain mechanism; the existing continuation does the work.

### 3.2 Note length is not stable-region length

The continuation loops only the stable body: after the attack, before the level falls 4 dB
under the body's median, less the crossfade and an alignment margin. On your recordings, cut
to a given length with a 60 ms release:

| Note | Stable region (real notes: 3 sung, 3 bowed; 2 bowed above 0.43 s, the third is that short) |
|---|---|
| 0.25 s | 0.065-0.085 s |
| 0.30 s | 0.095-0.14 s |
| 0.35 s | 0-0.17 s |
| 0.40 s | 0.01-0.22 s |
| 0.50 s | 0.11-0.33 s |
| 0.55-0.65 s | 0.15-0.48 s |
| 1.0 s | 0.27-0.83 s |

So **notes of 0.25-0.5 s have almost nothing loopable under either threshold**: they play as
recorded, with their own attack and release. The 0.3 s threshold changes notes of about
0.5-0.8 s, whose stable body is 0.3-0.5 s.

### 3.3 Results (6 real notes × 12 lengths, 0.3 s vs 0.5 s; renders held 2.5 s)

- **Newly sustaining:**
  - a sung note from 0.55 s: region 0.38-0.48 s, one or two loops of 0.28-0.29 s;
  - a bowed note at 0.55 s: region 0.31 s, a single 0.19 s loop.
- **Click measure** (largest second difference over RMS in the hold): 0.14-0.18, against
  0.12-0.21 for the same notes held from long regions.
- **Held pitch:** 3-5 cents (long regions: 4-6).
- **Level:** no dropouts in the holds.
- **Release:** each newly sustaining note ends with its own release (graft) after note-off.
- **Not forced:**
  - one sung note decays through its whole length (1 s too): one-shot at both thresholds;
  - a second sung note decays from 0.8 s on, and its shorter cuts have under 0.3 s of body;
  - the sustaining bowed note reads as decaying when cut at 0.6-0.7 s (it sustains again at
    0.8 s, from a region over 0.5 s);
  - a bowed note whose level keeps moving never has 0.3 s of stable body.
- **Regions ≥ 0.5 s:** identical at both thresholds (same jumps).

**Your call:** the bowed 0.55 s case loops one 0.19 s segment. By the numbers it is clean, but
a single short loop can sound like a flutter on long holds (§6).

### 3.4 Tests

`tests/integration/AtlasShortSustainTests.cpp`, synthetic notes of 0.35-1.2 s (a steady
bowed-like saw and a sung-like vibrato tone) and a pluck:

- regions under 0.3 s do not sustain ("stable region too short");
- regions of 0.3-0.5 s sustain at 0.3 and not at 0.5, with loops of at least 0.6 of the
  region;
- regions of 0.5 s or more give the same jumps at both thresholds;
- a held note keeps an even level (within 6 dB, never a dropout) and allocates nothing while
  it plays;
- a decaying pluck is not sustained;
- sample-set defaults are pinned.

## 4. Regressions and real-time safety

- Main tree ctest: see the commit; plugin tree: see the commit.
- Phase 0-2 goldens, the unified baseline, KEY DNA and the legacy OSP renders: unchanged.
- **Phase 3 atlas goldens:** the scenes with atlas regions of 0.3-0.5 s changed, because those
  notes now sustain (and their character and dynamics calibration, which read the
  continuation, follow). Proof: with only `minSustainSeconds` set back to 0.5 every Phase 3
  golden passes bit-identically. Updated with `OSP_UPDATE_GOLDEN=1` as an intentional DSP
  change (the commit says so).
- **Real-time safety:** no code on the render path changed. Shorter loops only mean more,
  equally short, jumps. Phrase analysis remains a background job.

## 5. Preparation speed

`research-renderer --prepare-bench` on the 13 Phase 4 bench recordings (5 of yours, 8 spliced
phrases), median of three runs each, Phase 4 and Phase 4B binaries one after the other on the
same VM:

| | Phase 4 | Phase 4B |
|---|---|---|
| Phrase analysis, all 13 | 4053 ms | 4084 ms (+1 %) |
| Stage A (playable), all 13 | 4602 ms | 4664 ms (+1 %) |
| Full build incl. continuation, all 13 | 12.26 s | 12.82 s (+5 %) |
| Member memory | 62 MB | 59 MB |

The analysis does a little more per dip and, rarely, describes the joined notes twice. The full
build is slightly slower because short notes that used to stop at "stable region too short" now
get a loop search. Run-to-run spread on this VM is of the same order, so preparation is
comparable to Phase 4.

## 6. Limitations and what needs your ears in Phase 5

- **Slow-dip repeats are kept whole by default** (organ re-presses, soft legato re-articulations
  on one pitch). They are flagged where the analysis can see a dip; repeats with no level change
  at all (legato sung on one pitch) are not found by anything level-based. DETECTION MORE
  restores Phase 4's splitting.
- **Bowed harmonics and harmonic tremolo** (a flageolet take, two harmonic-tremolo takes) still
  split, mostly at real voicing gaps and true octave flips that the repair does not fix. That is
  pitch tracking, not dips, and was not tuned further.
- **Organ single-pitch takes:** 16 extra regions remain. In the one take inspected by eye they
  are genuine re-presses after silence; the others were not verified.
- **Thresholds come from a small set:** 78 real takes, 8 + 5 spliced files and the lab fixtures.
  The numbers (6.5 / 9.1 / 9.75 dB per 10 ms, 9 / 20 dB, the 0.45 plateau share) separate what was
  measured with small margins in places, e.g. beating 0.35-0.38 against repeats 0.53+. They are
  not a guarantee on other instruments.
- **Accuracy claims are bounded:** the spliced corpora join real notes artificially, and no
  genuinely performed multi-note phrase has an exact truth yet. Nothing here is proof of
  accuracy on performed music.
- **Short sustains are untested by ear:** a 0.19-0.29 s loop with one or two jumps may sound
  mechanical on long holds. The renders are local (§7); please judge them in Ableton.
- **Short notes from a held take now start at its real attack.** With one region instead of
  several mid-note fragments, a short repeated note plays the beginning of the take. A take that
  starts with a slow crescendo therefore plays short notes softer: the nyckelharpa repetition
  render is about 12 dB quieter than before, but it is the instrument's real onset rather than
  a cut into the middle of a bow stroke. Whether that is the better instrument is for your ears;
  velocity layers or a NOTE MAP edit (moving the attack marker) are the remedies.
- **FEWER and NORMAL are close on held takes** (84 vs 87 extra regions). They differ on
  ambiguous dips (fewer flags) and sharper-attack requirements.

## 7. Renders (local, not in the repository)

Your recordings stay local. In the session scratchpad, `p4b-listen/`:

- `segmentation/`: five held takes - nyckelharpa, strings, tagelharpa beating, a sung hold and
  low strings - played as instruments with the Phase 4 and the Phase 4B atlas. The `long-hold`
  and `repetition` fixtures for each take, each before/after pair normalised with one gain.
- `sustain/`: the newly sustaining short notes at 0.3 s, with their 0.5 s versions (one-shots)
  for comparison, and long-note references.

## 8. Phase 5 preparation

Phase 5 (the NOTE MAP GUI on the existing ANDOR OSP layout and design system) can use this
directly:

- `possibleRepeat` / `repeatAt` give the uncertainty markers and the one-click split points.
- `phrase::edit::split` and `phrase::edit::mergeWithNext` are the existing edits (double-click to split,
  select two notes → Merge).
- DETECTION re-analysis goes through the existing queue and merge, and keeps corrections.

Not started.
