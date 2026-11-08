# SYNTHESIS S2.1 — Spectral imprint research: three experiments, one choice

Harness: `tests/unit/ImprintTests.cpp`.
- `osp_tests "[imprint]"` runs the unit tests.
- `OSP_IMPRINT_RESEARCH=<dir> osp_tests "[imprint-research]"` writes `measurements.md`,
  `listening-index.md` and `renders/` (plain and loudness-matched copies).

DSP: `src/engine/SpectralImprint.{h,cpp}` (pure C++).

**References are stand-ins**, by agreement: the musician's recordings are not in the build
container. They are built so that their resonances are known exactly:
- A, nyckelharpa: harmonics through a bowed body with four resonances, bow noise, vibrato;
- B, vocal: three vowel formants with strong breath;
- C, cello: three body resonances, a 12 dB crescendo that opens the upper harmonics;
- D, pluck: Karplus-Strong.

Every number below is about these stand-ins. The musical verdict is the musician's, on real
recordings, in the development build.

## 1. The three experiments

All three colour the existing SYNTH oscillator (S1's saw, its low-pass and envelope, untouched).
None replaces the sample path.

| | What it does | Data |
|---|---|---|
| **A, static** | A cascade of 24 band (bell) filters on the oscillator. Their gains are solved so the cascade meets the recording's sustain envelope at each band centre (each band's spill into its neighbours compensated, the accurate graphic-equaliser method). The output is levelled to the oscillator's own loudness, measured. | One sustain envelope per recording: cepstrally smoothed STFT (the lifter under the harmonic spacing), on a sixth-octave grid, its broad tilt mostly removed (35 % kept as colour), bounded at ±18 dB, mean 0 |
| **B, time-varying** | A's filters, the target moving through the recording's own frames: the attack's frames in order, then back and forth through the sustain (a ping-pong at the recording's pace, so no seam and no release while held). MOTION blends the moving frame with the static one. | The same envelope every 50 ms (at most 120 frames), with attack and sustain marked; a pluck without a steady sustain uses its early body (up to 0.3 s) |
| **C, filterbank** | A channel vocoder. The oscillator (carrier) and the voice's own sample output (contour) are split into the same bands. Each carrier band takes the contour band's share of the contour's envelope (3 ms / 30 ms followers, floored, at most +18 dB), and the result is levelled to the carrier. | None (real time) |

## 2. Measurements

**Timbre distance**:
- What it measures: the energy in each harmonic's region (k·f0 ± f0/2, a 65536-point FFT),
  compared with the reference played at the same key.
- Range: 150 Hz to 8 kHz, each octave weighing the same.
- The broad tilt and level are removed, because the oscillator keeps its own tilt and the
  imprint brings the body.
- Lower is closer.

Conditions: IMPRINT 100 %, MOTION 70 % for B, 24 perceptual bands.

| Reference | Key | Analog | A static | B time-varying | C filterbank |
|---|---|---|---|---|---|
| A nyckelharpa | −12 | 3.9 | 3.0 | 3.0 | 3.3 |
| | 0 | 2.7 | 2.4 | 2.5 | 2.1 |
| | +7 | 3.8 | 2.3 | 2.3 | 2.5 |
| | +12 | 2.3 | 1.5 | 1.5 | 1.8 |
| B vocal | −12 | 4.8 | **7.8** | **7.7** | 4.4 |
| | 0 | 4.4 | 4.5 | 4.5 | 3.4 |
| | +7 | 3.8 | 3.1 | 3.1 | 3.2 |
| | +12 | 4.0 | 4.0 | 4.0 | 3.8 |
| C cello (crescendo) | −12 | 7.1 | 4.3 | **3.1** | 5.7 |
| | 0 | 7.5 | 5.0 | **3.8** | 5.5 |
| | +7 | 8.0 | 4.7 | **3.4** | 5.7 |
| | +12 | 7.1 | 4.3 | **3.3** | 5.2 |
| D pluck | −12 | 15.4 | 13.5 | 14.2 | 12.9 |
| | 0 | 18.8 | 15.6 | 16.3 | 17.3 |
| | +7 | 23.7 | 21.1 | 21.9 | 22.5 |
| | +12 | 20.8 | 18.7 | 19.7 | 21.4 |

### Level against Analog (dB, the oscillator alone)

| Method | Range across all references and keys |
|---|---|
| A | within 0.1 dB, except the cello at +7: +2.4 (its crescendo) |
| B | within 0.2 dB, except the cello at +7: +3.1 |
| C | −0.4 to +3.6 dB (worst on the pluck at −12) |

### Bands and spacing (distance at the root)

16, 24 and 32 bands, log or ERB spacing, differ by 0.0-0.3 dB for A. For C, more bands help
the vocal (3.9 at 16 log, 2.9 at 32 ERB).

### Formant follow (internal setting; the vocal, A)

| Key | Follow 0 (fixed resonances) | 0.5 | 1.0 (resonances move with the key) |
|---|---|---|---|
| −12 | 7.8 | 7.6 | 7.1 |
| +7 | 3.1 | 3.9 | 4.6 |
| +12 | 4.0 | 5.6 | 6.6 |

Fixed resonances are closer upward and about even downward. The default is fixed (0); the
setting stays internal.

### After the sample ends

A pluck held 3 s; level 1.5-2.5 s against 0.2-0.6 s:

| Analog | A | B | C |
|---|---|---|---|
| +0.0 | +0.0 | −0.3 | −1.1 |

- A and B keep their colour after the sound has gone: the descriptor does not depend on it.
- C has nothing left to follow. Its levelling then amplifies the bands' remains (its renders
  show it).

### Cost

This build machine: shared Linux x86-64, 48 kHz. Figures are ns per sample per sounding voice,
median of 5. Apple Silicon is faster; the ratios are the point.

| Stage | 1 voice | 8 | 16 | 32 |
|---|---|---|---|---|
| Oscillator alone (Analog) | 4.1 | 4.1 | 4.1 | 4.1 |
| + A, 24 bands (settled) | 82 | 82 | 125 | 107 |
| + B, 24 bands, MOTION 50 % | 157 | 115 | 97 | 99 |
| + C, 24 bands | 255 | 256 | 314 | 318 |
| + A, 16 / 32 bands | 81-103 / 204-269 | | | |
| + C, 16 / 32 bands | 213-254 / 388-570 | | | |

(The 16- and 32-band shapers take the general path; 24 reads the descriptor's ready band values.)

So 16 voices of B at 24 bands cost about 1.6 µs per sample, about **7-8 % of this machine's
core**, and only while voices sound in Imprint with IMPRINT above 0.

| | |
|---|---|
| Latency | A and B: none (minimum-phase IIR, the first target taken at the note's start). C: its followers, 3 ms attack / 30 ms release |
| Analysis | 30 ms for a 3 s recording, off the audio thread |
| Descriptor | about 18 kB for 60 frames: small enough to rebuild with every model, nothing to cache on disk |
| SIMD | not attempted. The cascade is latency-bound (each band feeds the next), so there is no lane parallelism to use, and no speed-up is claimed |

## 3. The choice: B, time-varying (with MOTION 0 being A)

- **Musical similarity:**
  - A and B move the oscillator towards the recording on the nyckelharpa (up to 1.5 dB closer)
    and the pluck (2-3 dB).
  - B is clearly closest on the cello, whose timbre opens as it swells: 3.1-3.8 against 7.1-8.0
    for Analog, and 4.3-5.0 for A.
  - C is best on the vocal at its root (it hears the breath live), but worse on the cello and
    the high pluck.
- **Sustain and lifetime:**
  - A and B are valid for the whole note, after the sample ends, in One Shot, Granular and
    Atlas.
  - C needs the sample to be sounding, so it breaks the S1 promise that a held pluck becomes a
    held synthetic tone.
- **Cost:** C costs about 2.5-3 times A or B and adds a second analysis bank per voice.
- **Complexity:** A and B need nothing new on the audio thread beyond the filters.
- **Verdict:** C is significantly more expensive without a clearly better result, so the
  offline approach wins, as the brief anticipated. B with MOTION covers A (MOTION 0 is exactly
  the static imprint), so production ships one engine with two controls.
- **Retained for research:** C stays in `SpectralImprint` and the harness.

## 4. What is uncertain

- **The vocal an octave down:** A and B measure worse than Analog there (7.8 against 4.8).
  - A voice at E4 shows its envelope only at its harmonics, 330 Hz apart. The profile cannot
    know the narrow formants between them, so its broad hump also lifts the troughs a lower
    key reveals.
  - Real voices may differ.
  - If this is audible, the remedies are: a profile made from more than one key (an Atlas
    has several), or a gentler IMPRINT at keys far below the recording.
- **Metallic or robotic character, smearing, and "one sound, not two"** are listening
  questions. The renders are there for them (§5); the numbers above do not answer them.
- **Real recordings:** none were available here.

## 5. Listening files

Run the harness (above) to get them.

- Per reference:
  - the source;
  - Analog at SYNTH 25 / 50 / 75 / 100 %;
  - A at SYNTH 25 / 50 / 75 / 100 % with IMPRINT 50 / 100 %;
  - B at MOTION 30 and 70 %;
  - C at SYNTH 50 and 100 %.
- Every file comes plain and loudness-matched (`-matched`, −20 dBFS RMS).
- `listening-index.md` lists them.
