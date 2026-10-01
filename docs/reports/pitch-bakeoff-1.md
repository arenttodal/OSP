# Pitch bake-off 1 — results and decision

Plan: `research/bakeoff/plan-1.json` (seed 1). There are 5 sources, each a family
representative, at 5 offsets (−24, −12, 0, +12, +24 semitones), rendered by 3 engines.
That gives 75 clips in 25 blind groups.

- **A**: bandlimited resampling (the baseline sampler).
- **B**: Signalsmith Stretch 1.4.0 with plain transposition.
- **C**: Signalsmith Stretch with formant compensation. Its formant base is the analysed F0.

Every clip starts at the analysed onset and gets the same level preparation. It is
trimmed to its group's common length, faded and RMS-matched to −20 dBFS.

The numbers in the guard-rail section are measurements only. The decision comes from
the blind listening ratings in `research/bakeoff/pitch-bakeoff-1.ratings.json`, which
are scored with `research-renderer --bakeoff-score`.

## Listening results (one listener, 2026-10-01)

21 of 25 groups have a "best" pick. The missing ones are the bowed, synth and vocal 0
controls and bowed +24. The listener mostly rated only the take they preferred, plus
any clearly broken ones, so the **wins** are the signal. The means only cover the
takes that were rated.

Winner per group, shown as identity/beauty/artifacts of the winning take:

| family | −24 | −12 | +12 | +24 |
|---|---|---|---|---|
| organ | **B** 5/5/5 | **B** 5/4/5 | **B** 5/5/5 | **B** 5/5/2 |
| vocal | **A** 3/4/3 | **A** 4/4/4 | **B** 5/5/5 | **B** 4/3/3 |
| bowed | **A** 4/4/4 | **B** 4/4/4 | **A** 4/4/3 | — |
| synth | **A** 5/5/5 | **A** 4/4/4 | C 4/4/3 | **A** 4/4/4 |
| pluck | **B** 5/5/4 | **B** 4/4/4 | **A** 4/4/4 | **A** 4/4/4 |

Wins outside the 0 controls: **A 9, B 9, C 1**.

**C was rated 1/1/1 every time it was scored away from the root**: 8 times across organ,
vocal and bowed at ±12 and ±24. Its only non-control win was synth +12.

### Reading

- **Organ: B 4 of 4.** Resampling changes the speed of the bellows and tremulant
  movement and the length of the note. B keeps the instrument's time behaviour.
- **Vocal and pluck split by direction.** B wins where resampling's speed change is
  unnatural: a voice sped up (chipmunk, fast vibrato), and a pluck slowed down (a
  smeared, slow attack and an overlong decay). A wins where its speed change sounds
  plausible: a voice going down, and a pluck going up (a higher string decays faster).
- **Synth: A.** Static spectra lose nothing to resampling, and A has no phasiness.
- **Bowed: mixed** (A, B, A).

## Decision

1. **Engine C (formant compensation via Signalsmith) is rejected** as a default or
   branch. It stays in the research tool only. Formant control returns later as a
   CHARACTER / Reimagined transformation on top of the chosen branch (Phase 6), not as
   the pitch engine.
2. **A (resampling) stays the default.** Overall it ties with B, and the rule set
   before listening keeps the simpler engine on a tie (spec §24: do not "improve"
   resampling automatically).
3. **B becomes a second pitch branch, "Natural"**, built as offline register anchors
   (spec §27) at −24, −12, +12 and +24. Notes resample from the nearest anchor (at most
   ±6 semitones), so the audio thread does the same cheap work as A. The anchors are
   built by the render/cache worker after the instrument is already playable.
4. In the plugin, **Pitch Character** (Advanced panel, spec §13) chooses between
   *Tape* (A) and *Natural* (B). The Original ↔ Reimagined control may blend between
   them later.
5. The evidence is one listener and one source per family. Whether *Natural* should be
   the automatic default for some sources (organ-like sustains, upward vocals, downward
   plucks) is an **open question** for a second round with more sources per family.

## Pitch accuracy and spectral-envelope shift

Each cell shows two numbers for one clip:

- **cents**: the pitch error of the rendered clip against the target note.
- **env st**: the shift of the cepstral spectral envelope against the source, in
  semitones. Zero means the timbre envelope stayed in place.

| family | offset | clip s | A cents / env st | B cents / env st | C cents / env st |
|---|---:|---:|---|---|---|
| organ | −24 | 3.24 | +0 / +3.4 | −10 / +2.6 | **−1191** / +9.8 |
| organ | −12 | 3.24 | +1 / −3.0 | +2 / −2.9 | −0 / +4.2 |
| organ | 0 | 3.25 | +1 / −0.1 | +1 / −0.1 | +1 / −0.1 |
| organ | +12 | 3.24 | +1 / +1.2 | +0 / +1.0 | +2 / −2.4 |
| organ | +24 | 3.22 | +1 / +2.9 | +1 / +2.1 | **−1906** / +0.7 |
| vocal | −24 | 3.21 | −2 / +5.9 | −5 / −2.3 | −4 / +12.6 |
| vocal | −12 | 3.20 | −0 / −10.8 | −0 / −10.3 | +0 / +10.2 |
| vocal | 0 | 3.22 | +0 / +0.0 | +0 / +0.0 | +0 / +0.0 |
| vocal | +12 | 2.40 | +1 / +10.2 | +1 / +9.9 | +2 / +3.5 |
| vocal | +24 | 1.20 | +3 / +18.0 | +2 / −4.6 | +4 / +3.4 |
| bowed | −24 | 3.18 | −11 / +2.7 | −13 / +0.4 | +8 / +3.5 |
| bowed | −12 | 3.18 | −13 / −1.6 | −10 / −0.2 | −8 / +3.2 |
| bowed | 0 | 3.19 | −12 / +0.0 | −12 / +0.0 | −12 / +0.0 |
| bowed | +12 | 3.18 | −9 / −0.0 | −11 / −0.3 | −25 / −0.0 |
| bowed | +24 | 3.18 | −5 / +0.4 | −11 / −8.9 | **−1213** / +0.0 |
| synth | −24 | 3.23 | +0 / −7.3 | −2 / −10.8 | −2 / −0.8 |
| synth | −12 | 3.25 | +0 / −7.2 | −1 / −5.6 | +0 / −1.1 |
| synth | 0 | 3.25 | +0 / −0.6 | +0 / −0.6 | +0 / −0.6 |
| synth | +12 | 3.25 | +0 / +10.1 | +1 / +12.4 | +4 / +10.5 |
| synth | +24 | 2.46 | +0 / +18.1 | +1 / +24.1 | +4 / +21.7 |
| pluck | −24 | 2.58 | +2 / +0.2 | −3 / −0.8 | +6 / +14.1 |
| pluck | −12 | 2.58 | −0 / −1.7 | −2 / −1.6 | +5 / +5.9 |
| pluck | 0 | 2.58 | −0 / +0.3 | −0 / +0.3 | −0 / +0.3 |
| pluck | +12 | 1.29 | +0 / +3.9 | +1 / +4.1 | +3 / +0.4 |
| pluck | +24 | 0.65 | +2 / +4.9 | +3 / +4.2 | +10 / +3.8 |

The bowed source sits about 12 cents flat of its rounded root in every row, which is
why its 0-offset clips read −12 for all three engines.

Render cost per 3 s clip, offline on a Xeon VM:

| engine | median | max |
|---|---:|---:|
| A | 72 ms | 249 ms |
| B | 364 ms | 943 ms |
| C | 372 ms | 954 ms |

## Observations

1. **A and B are pitch-accurate everywhere.** Their median |error| is 0.6 and 1.6
   cents, and none exceeds 15 cents.
2. **C breaks at the extremes.** Three of its 25 clips read an octave or more off
   target:
   - organ −24 and +24;
   - bowed +24, which has strong sub-octave energy (−13.5 dB);
   - the organ −24 fundamental is about 11 dB down.

   These are artefacts of formant compensation with a strong F0 hint at ±2 octaves,
   not a tracker failure: B, which uses the same library without compensation, is
   clean on the same notes. C should be judged by ear at ±12 and treated as suspect
   at ±24.
3. **On synthetic material the envelope metric does what it should.** A 220 Hz vowel
   gives:
   - C below 0.6 st at ±12, which preserves formants;
   - A and B follow the transposition.
4. **On real sources the envelope metric is not trustworthy.** For example, vocal −12
   reads −10.8 st for A but +10.2 st for C. The reasons:
   - with high F0s (vocal at about 660 Hz), the cepstral lifter cannot separate the
     envelope from the harmonics;
   - for broadband synths, the "envelope" is mostly the harmonic slope.

   It stays as a guard rail for synthetic tests only. Listening decides.
5. **The common-length rule shortens upward groups** (pluck +24 is 0.65 s, vocal +24
   is 1.2 s). A plays the source faster, so its natural length sets the group length,
   and listeners hear less of B and C than they would in use. This is acceptable for
   judging timbre. Duration and sustain are judged later with the Continuation Engine.
6. **Cost.** B and C are about 5× the offline cost of A. Both are offline here, as
   pre-render per note. Real-time use would need a streaming design and its own
   benchmark.

## Decision rule (set before listening)

- Keep **A** as the default wherever it ties or wins. It is the cheapest and simplest
  (spec: "prefer the simplest algorithm that wins the listening test").
- Adopt B or C only for the families and offset ranges where it wins clearly on
  identity and beauty without losing on artifacts.
- Take C only where the ±24 failures above are absent or avoided by a range limit.
