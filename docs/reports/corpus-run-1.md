# Corpus run 1 — full research corpus, baselines A and B

_Run: 80/80 files of `research/corpus/MANIFEST.txt`, `--profile standard --engines A,B
--jobs 4`, analyzer 0.1.0, fixtures v1, seed 1, 48 kHz output. Linux VM (Xeon 2.1 GHz)._

## Result: Phase 0 exit condition met

| | |
|---|---|
| files | **80 analysed, 0 failed**, 0 unsupported, 0 duplicates |
| renders | **800: 766 ok, 34 warning, 0 error** (no NaN/Inf, clipping, channel or rate errors) |
| pitch confidence | 66 high, 11 moderate, 3 low, 0 none |
| wall time | 2 min 47 s for analysis + 800 renders + metrics (4 workers) |
| analysis speed | longest file (28.8 s stereo) analysed in 1.9 s including load and hash |

Corpus shape: 65 files at 48 kHz, 15 at 44.1 kHz; 78 stereo, 2 mono; 68 are 32-bit
float WAV, 10 are 16-bit, 1 is 24-bit, 1 is a 24-bit AIFF. Durations range 0.69–28.8 s,
median 4.7 s. 41 of the "stereo" files are effectively mono (dual mono or L/R
correlation > 0.99); 21 are genuinely wide (width > 0.2).

## Per family

| Family (folder) | Files | Confidence | Detected roots | Notes |
|---|---|---|---|---|
| trampeorgel | 16 | 16 high | C3 C4 C5 C6, F2 F3 F4 F5 | A ready-made **register set**: C and F across four octaves. Consistently 6–23 cents sharp (the instrument's tuning, not an analysis error). Slow tone onset (0.4–0.55 s of bellows before the reed speaks) and quiet recordings (−30 to −35 dBFS peak). |
| tagel-puck | 14 | 9 high, 5 moderate | B3, E4, E3 | Plucked tagelharpa. The moderate ones split between E3 and its fifth B3 (drone strings sounding together), so a single root is genuinely ambiguous. Two groups suit round-robin work: B3 ×9 and E4 ×2. |
| plucks | 16 | 13 high, 1 moderate, 2 low | mostly E4 (+ A4, B4, F#2) | Repeated-performance set at E4. The low-confidence ones mix the pluck pitch with a lower resonance (e.g. B4 45 % / E3 34 %). |
| sus | 8 | 5 high, 3 moderate | A#3 A#4 D#4 D#5 F5 F6 | **Filenames vs detected roots differ consistently** (see below). The three `Harm … Trem` files are moderate because frames alternate between the harmonic and the pitch an octave and a fifth below (exactly 3:1). |
| violin | 4 | 2 high, 1 moderate, 1 low | A#4, A5, F4, A4 | `RR A#_01` = A#4 (correct). `RR A#_09` sounds an octave up: its energy is at ~935–948 Hz with the 466 Hz band 20 dB down — a different articulation, honestly reported as low confidence. `Flageolett` splits between A4 and A5 (harmonic). |
| vocals | 8 | 7 high, 1 moderate | A2 A4 A5 D5 E5 F5 | The moderate `MOODS 100936` splits between A2 and E4 (3:1 again: a low voice with a strong 3rd harmonic, or the reverse). |
| nyckelharpa | 4 | 4 high | G4 A4 C5 D#5 | Bowed, slow swells (attack up to 5 s), long (13–28 s): good continuation material. |
| misc | 10 | 10 high | C2 C#2 G1 C4 C#4 F4 G4 G5 A5 E5 | Reese bass `G` correctly found at G1 (49 Hz); Prophet 6 at C2; 808 at C#2; sax at G5; Analog/ORG/F/TREM freezes all high. |

## Observations that need your input

1. **`Sus` filenames.** Detected roots are offset from the names, with near-zero cents
   and 95–99 % confidence: `Sus A Med` → A#4, `Sus D Med` → D#4, `Sus E Medium` → F5,
   `Sus G Slow` → A#3, `Sus Harm G Trem` → A#4. That's +1 semitone for A/D/E and +3 for G.
   A detector does not make clean semitone errors, so either the names refer to something
   other than sounding pitch (string, fingering, a transposed session), or the recordings
   were pitch-shifted. Worth checking by ear.
2. **Which root for harmonics?** For the `Harm … Trem` files the detector alternates
   between the sounding harmonic and the pitch 19 semitones below it. Musically the
   root should probably be the sounding harmonic; the analyser currently reports the
   majority and, honestly, moderate confidence.

## Render warnings, explained

All 34 warnings are expected behaviour of baseline A on real material, not engine faults:

- **13 `near-silent`, all in `repetition`** (0.4 s notes). Six sources take longer than
  0.4 s to start sounding (organ bellows, a 15 s freeze swell, a sung entry at 0.5 s), and
  several are quiet recordings. Baseline A plays from sample 0, so the short notes end
  before the sound arrives.
- **21 `pitch-mismatch` renders (73 notes)**. The metric compares the first 0.5 s of each
  note with the file's median pitch. Mismatches come from the sources themselves:
  octave/twelfth alternation (`Sus Harm E Trem`, two organ notes), the scoop into a sung
  note (−175 cents in `VOK MOODS 100821`), and pitch overshoot at pluck or freeze onsets
  (+55 cents). Transposition in engine A is exact by construction and covered by unit
  tests.

## Product findings (for Phase 1/3 decisions — not changed in baseline A)

1. **Start at the sound, not the file.** 20 of 80 sources have an attack longer than 1 s
   and 6 have an onset after 0.3 s. A musician playing the organ sample in the plugin
   would wait half a second for each note. Recommendation: a playback start offset taken
   from analysis (e.g. just before `onsetSeconds`), as an engine option compared against
   baseline A — not a change to baseline A itself.
2. **Level.** 36 of 80 sources peak below −20 dBFS (lowest −35.8 dBFS). The source must
   never be normalised destructively, but a non-destructive playback gain derived from
   analysis would make instruments comparable. Again an option, judged by listening.
3. **Sustain is first class for real.** 23 of 80 recordings end while still sounding (no
   natural release), and 13 show tremolo. These are the Continuation Engine's test cases.
4. **Ready-made multi-sample sets.** Trampeorgel (C/F register anchors over four
   octaves), Tagel B3 ×9, Plucks E4 ×10 and the violin RR pair give Phase 4 (performance)
   and Phase 7 (register) ground truth without new recordings.
5. **Vibrato detection is conservative.** Only 2 files report vibrato (it requires a
   long stable run within ±100 cents); expressive vocals with glides don't qualify. Fine
   for now; revisit if the Performance Engine needs vibrato parameters.

## Decisions (after review)

- **Trust the sound, not the filename.** The `Sus` names may be pitch-shifted or simply
  wrong; detected pitch is authoritative. No special handling.
- **Harmonics: the root is the sounding pitch.** For the `Harm … Trem` files the majority
  pitch is already the sounding harmonic (F6, F5, D#5, A#4), so the current root choice
  stands; their moderate confidence remains as an honest signal of the alternating
  sub-pitch.
- **Start at the sound and match levels.** Implemented as non-destructive playback
  options (`PlaybackOptions`: `startAtOnset`, `normaliseLevel`), off in baselines A/B,
  on in the plugin and in `research/configs/prepared-a.json`.

Effect on the corpus (`--engines A --start onset --level normalise`, standard profile):

| | plain A | prepared A |
|---|---|---|
| renders with warnings | 17 / 400 | 3 / 400 (all source pitch behaviour) |
| `near-silent` | 13 | 0 |
| chord-render peak level across the corpus | −50.1 … −5.9 dBFS (44 dB spread) | −24.8 … −11.8 dBFS (13 dB spread) |

## Reproduce

```sh
./build/apps/research-renderer/research-renderer --corpus research/corpus --profile standard --engines A,B --jobs 4
# reports: research/reports/summary.md, renders: research/renders/ (~2.8 GB)
```
