# PEDAL LOOP × REIMAGINED, PL-R4: regression and listening results

Branch `claude/pedal-reimagined`. PL-R0's reproduction (`00-audit.md`) showed TAPE FRAME,
TOYBOX, MOSAIC and MIRAGE falling back to Normal above 0 %. PL-R1–R3 made every mode loop.
This gate measures the result.

## Automated regression (`osp_tests "[pedal-loop]"`, all passing)

| test | covers |
|---|---|
| the shared fold loops in bounds, continuously, and lets go | `PedalFold`: Forward and Ping-Pong stay in the region; no jump in a periodic signal across wraps and turns; release fades back to ordinary playback |
| a late pedal fades into the last region at the note's level; too late, nothing | 50 ms entry, level matched to the note (−12 dB here), return where it found the recording; within 2 ms of the end, no arming |
| every mode, every amount, Forward and Ping-Pong hold the note from one attack | 5 modes × 0 / 1 / 25 / 50 / 75 / 100 % × Forward / Ping-Pong. The note sounds at twice the recording's length; no silence; no repeated attack or click (no rise above 9 dB); the read really loops (it moves backwards: Forward wraps, Ping-Pong turns); pedal up releases and ends the note |
| an unused or Normal pedal loop leaves every mode exactly as it was | Forward or Ping-Pong selected with the pedal never pressed: **bit-identical** to Normal, every mode at 50 and 100 %. Normal with the pedal down: the note still ends with the recording |
| pressed during the note, pressed late, lifted with the key held, toggled fast | every mode × Forward / Ping-Pong. Late pedal level-matched; pedal up with the key held resumes the recording to its end; 24 toggles in 0.26 s are safe and the last state wins |
| chords, stealing, All Notes Off, pitches and sample rates | three-note chords at 45 / 57 / 81 from 44.1 and 96 kHz sources, all looping; 30 notes through stealing; All Notes Off silences |
| the mode's character stays audible while the loop holds | through the held loop, 100 % differs clearly from 0 % in every mode (more than 10 % of the signal energy); at 0 % every mode is the plain loop, sample for sample |
| a short region (a percussive hit) holds in every mode, TOYBOX's legs inside it | the defect the lab found (below) |
| allocation (Pedal Loop 1.0's `[realtime]` test) | unchanged, passing |

The full core suite (unit, integration, regression with every golden) passes.
The full plugin build and suite pass too: `ctest` in `build-plugin`, 10/10 (unit, integration, regression, plugin, visual references, examples).

## Listening matrix (`research-renderer --pedal-reimagined-lab`)

The spec's section 16, for every source and mode.
- **Renders:**
  - A: REIMAGINED off, with the pedal loop;
  - B: 50 %, with the pedal loop;
  - C: 100 %, with the pedal loop;
  - each of A, B and C with Forward and Ping-Pong;
  - D: 100 % with Normal.
- **Performance:** the pedal down, the note at 0.1 s, the key up at 1 s, the pedal up at 8 s.
- **Result:** 216 renders, all finite. Full tables: `lab/pedal-reimagined-lab.md`.

**The sources are synthetic stand-ins**:
- piano (inharmonic decay);
- a nyckelharpa-like bowed note (vibrato, bow noise, sympathetic strings);
- a cello;
- a sung vowel and a short sung phrase;
- a plucked string;
- an organ;
- a percussive hit.

The research corpus (your recordings) is local to your machine and git-ignored, so it is not in
this environment. The same command runs on the recordings:

```sh
./build/apps/research-renderer/research-renderer --pedal-reimagined-lab out \
    --recordings "research/corpus/piano.wav,research/corpus/nyckelharpa.wav,..." --only-recordings yes
```

**Summary across the eight sources:**
- held: the level 7–8 s, well after each 0.8–4 s recording;
- click: the worst 5 ms of high-passed energy over the held loop against the 40 ms around it.
  A click stands out by 15 dB or more;
- rise: the largest jump of the 10 ms level, which is how a repeated attack shows. The sung
  phrase is excluded because its later notes are its own attacks;
- quietest: the quietest 50 ms against the held level.

| render | held dB (min .. max) | worst click (dB) | worst rise (dB) | quietest (dB) |
|---|---|---|---|---|
| A-off-forward | -44.9 .. -31.5 | 3.4 | 2.0 (percussive) | -4.8 |
| A-off-pingpong | -43.2 .. -31.6 | 3.4 | 7.9 (cello) | -4.8 |
| kaleidoscope-B-050-forward | -45.6 .. -31.3 | 3.7 | 3.4 (percussive) | -5.6 |
| kaleidoscope-C-100-forward | -47.1 .. -36.2 | 9.4 | 18.1 (percussive) | -12.1 |
| kaleidoscope-B-050-pingpong | -43.8 .. -31.4 | 6.6 | 8.8 (percussive) | -5.7 |
| kaleidoscope-C-100-pingpong | -47.0 .. -36.2 | 7.5 | 15.4 (percussive) | -15.7 |
| tape-frame-B-050-forward | -49.4 .. -34.5 | 4.1 | 4.3 (percussive) | -5.6 |
| tape-frame-C-100-forward | -50.7 .. -34.7 | 4.1 | 5.4 (percussive) | -5.7 |
| tape-frame-B-050-pingpong | -49.5 .. -34.4 | 3.3 | 7.4 (cello) | -5.9 |
| tape-frame-C-100-pingpong | -49.6 .. -34.5 | 3.2 | 7.5 (percussive) | -6.0 |
| toybox-B-050-forward | -43.7 .. -30.5 | 5.3 | 8.1 (percussive) | -5.2 |
| toybox-C-100-forward | -43.6 .. -30.4 | 6.4 | 8.6 (percussive) | -5.0 |
| toybox-B-050-pingpong | -42.6 .. -30.5 | 6.4 | 5.0 (percussive) | -5.3 |
| toybox-C-100-pingpong | -42.4 .. -30.4 | 6.2 | 5.4 (percussive) | -5.9 |
| mosaic-B-050-forward | -44.9 .. -33.8 | 5.1 | 5.5 (guitar) | -4.4 |
| mosaic-C-100-forward | -44.9 .. -29.9 | 4.6 | 5.3 (guitar) | -8.1 |
| mosaic-B-050-pingpong | -43.2 .. -33.7 | 3.4 | 7.2 (percussive) | -4.7 |
| mosaic-C-100-pingpong | -43.2 .. -29.9 | 3.4 | 7.2 (percussive) | -8.5 |
| mirage-B-050-forward | -46.7 .. -34.0 | 5.1 | 4.4 (percussive) | -4.5 |
| mirage-C-100-forward | -45.9 .. -33.3 | 7.0 | 4.4 (percussive) | -6.6 |
| mirage-B-050-pingpong | -46.7 .. -34.0 | 3.9 | 8.1 (cello) | -4.5 |
| mirage-C-100-pingpong | -46.0 .. -33.2 | 7.8 | 9.1 (cello) | -6.7 |

Reading it:
- **Every mode holds every source** in both directions, at 50 and at 100 %. There are no
  clicks: the worst is 9.4 dB, against the 15 dB a click needs.
- **The outliers are not new behaviour:**
  - KALEIDOSCOPE at 100 % on the percussive hit rises 15–18 dB. Its Normal render (D) rises the
    same 18 dB without the pedal loop: the mode's doubling repeats the transient.
  - The cello's Ping-Pong turns rise 7–9 dB. That includes REIMAGINED off (A), i.e. Pedal Loop
    1.0's own turns on this source.
- **MOSAIC cannot rebuild the sung phrase and the hit.** It plays them as recorded (unchanged
  behaviour), so the loop is the voice's and B/C equal A.
- **Tape wear:** TAPE FRAME holds 2–6 dB below A, because each pass wears the tape (its own
  `nextPassGain`, floored at −6 dB).

**Found and fixed here.** TOYBOX did not hold the percussive hit at 50 % (silent at 7–8 s).
- Cause: its default TURN walk plans a leg about 0.8 s long when it restarts at the region's
  start, and a pedal region on a short sound is 0.12 s. The head ran out of the region and on
  to the end of the recording.
- Fix: while the pedal holds the note, that leg is clamped to the region. It is unchanged
  otherwise, so goldens are unaffected.
- After the fix: −43.7 dB held, no dip. Covered by the short-region test.

**Pre-existing, not changed:** TAPE FRAME and MIRAGE with Normal (D) end abruptly at the
recording's end. This is their engines' `done`, and it is identical before and after this work
(the Normal path is bit-identical).

## CPU (`--bench yes`)

16 notes held under the pedal, REIMAGINED 100 %, 48 kHz. Share of one core (median of three),
with no other load:

| mode | Normal | Forward | Ping-Pong | Forward / Normal |
|---|---|---|---|---|
| kaleidoscope | 14.46 % | 14.75 % | 15.08 % | 1.020 |
| tape-frame | 18.51 % | 19.33 % | 21.37 % | 1.044 |
| toybox | 8.34 % | 9.67 % | 9.74 % | 1.160 |
| mosaic | 16.60 % | 15.27 % | 13.79 % | 0.919 |
| mirage | 7.94 % | 8.25 % | 7.42 % | 1.039 |

The loop costs at most a few tenths of a percent of one core per 16 voices. The worst ratio is
TOYBOX at +16 %, i.e. 1.3 percentage points of one core. MOSAIC is cheaper looping than with
Normal: its rebuilt spectrum holds a body region instead of tracking the evolving recording.

## Known limitations

1. **TAPE FRAME with the layer's LOOP on.** Its spliced tape sustains by rewinding, which is its
   own forward loop. Pedal Ping-Pong is not applied there: a tape does not run backwards in
   this engine. With LOOP off (the reported case) Forward and Ping-Pong both work. Forward
   with LOOP on stays with the LOOP everywhere (Pedal Loop 1.0's precedence).
2. **MOSAIC with LOOP on** already holds and ping-pongs the body. The pedal loop then applies to
   its dry part only.
3. **Listening on real recordings is still to do.** The stand-ins are deterministic and cover
   the families, but they are not the recordings. Use the command above, or the development
   build.
