# SYNTHESIZE S2 — the SYNTH page

S1's listening was good; the owner asked for a minimal page of key controls, opened by
clicking SYNTH, in the EQ / DAMPEN overlay style. Six controls per source were agreed (A, B
and C each have their own).

## The page

A single click on a source's SYNTH chip opens the page over that source's waveform (dragging
the chip still sets the amount; a double-click still returns it to 0). One page at a time
holds the well: EQ, NOTE MAP, SYNTH. The top row: **SYNTH A** ("Off" beside it at 0 %),
**AMOUNT** (the chip's value), close. The middle: the oscillator's own wave, computed from
the real `synth::Voice` at A3 after its tone filter, so WAVE, TONE, OCTAVE (more or fewer
cycles) and DETUNE are seen; dashed while the amount is 0. The foot (two rows in narrow
cards):

| Control | Values | What it does |
|---|---|---|
| WAVE | Saw, Square, Triangle, Sine | PolyBLEP saw (S1's, unchanged) and square; a plain triangle (its corners fall at 12 dB/oct, the low-pass does the rest); sine. Each scaled to the saw's level after the filter (within 1.5 dB, tested). |
| OCTAVE | -2, -1, 0, +1 | The oscillator's octave; a sub-octave under 20 Hz is raised until audible. |
| DETUNE | 0-50 cents | Two oscillators either side of the pitch at -3 dB each (the same power); 0 is one oscillator, as S1. Eased in and out over ~20 ms. |
| TONE | 0-100 % (50 = S1) | The 12 dB/oct low-pass from 1/4 to 4 x S1's cutoff; velocity still opens it a little. |
| SHAPE | Held, Follow | S1's envelope A (held while the key is) and B (the recording's contour, settling to full sustain). Per source; the development audition's Follow still applies to every source. |
| PITCH | Keys, Recording | Keys: S1 (the played key in equal temperament, bends and TUNE followed). Recording: the pitch the recording is heard at, at the voice's read position, from its analysis' pitch track (`AnalysisData::pitch.trackHz`): its tuning and its vibrato, held through unvoiced frames, ~5 ms smoothing, frames more than a semitone from the root ignored. The root correction is followed (it is heard). Granular voices use Keys (their read position is not one timeline). |

## Parameters and compatibility

Per layer, version hint 23, appended after SYNTHESIZE: `layerA.synth.wave` (choice),
`.octave` (int -2..1), `.detune` (0-50 cents), `.tone` (0-100 %, default 50), `.shape`
(Held / Follow), `.pitch` (Keys / Recording). The defaults are S1's oscillator to the
sample (tested: the voice and the engine render identically with and without the page at its
defaults); a session from before has none of them and plays as it did. The parameter-schema
golden gained exactly these 18 lines. Research configs take a per-layer `"synth"` object
(`docs/analysis-schema.md`).

## Tests

- `[unit][synth][synthpage]`: the defaults are S1 sample for sample (voice and engine); each
  wave at its pitch, its level against the saw, its harmonics (no even ones for square,
  triangle, sine; none above the first for sine); a high square's aliasing below -40 dB;
  OCTAVE (including the raised sub-octave), DETUNE (both partials, same loudness), TONE
  (upper harmonics rise from dark to bright); live changes without a click or an allocation;
  SHAPE per source equals the audition's Follow; PITCH Recording follows a +-30 cent vibrato
  (at least 60 % of its swing, within 25 cents window by window; Keys holds within 4 cents)
  and a root correction.
- `[plugin][synthpage]`: 18 parameters at hint 23, heard, deterministic, saved and reopened;
  an older session plays exactly as S1; changing the page on held notes allocates nothing;
  the chip opens the page in the display's place; choices are one undo step each; one page at
  a time; A, B and C independent.
- Screenshots: `OSP_REFINE_DIR=<folder> osp_plugin_tests "[refine-shots]"`, the `07-synth-page-*`
  states (1, 2 and 3 cards, and off).

## Known limits

- PITCH Recording follows the analysed track: a recording whose pitch is not detected (noise,
  a chord) stays at Keys; very fast ornaments are smoothed.
- DETUNE doubles the oscillator's cost on the voices that use it (one more wave per voice;
  the S1 measurement put one oscillator at a small share of a voice).
- The release question from S1 (§8.2: at high amounts the oscillator's own ~0.2 s release is
  heard instead of R) is unchanged.
