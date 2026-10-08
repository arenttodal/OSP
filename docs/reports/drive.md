# DRIVE: the macro that replaced DYNAMICS on the panel

A shared saturation stage with three circuits (TUBE, TAPE, CRUNCH) on the DYNAMICS macro's
position. DYNAMICS itself is unchanged and lives in Advanced now. Parameters v14; state
version unchanged (only parameters were added).

## 1. Audit (DRIVE-00)

What the code was before the change (the code, not earlier design notes, was authoritative):

1. **DYNAMICS parameters.** `dynamics` (the macro, 0-100 %, default 65, version hint 2),
   `dynamics.curve` (Soft / Linear / Hard, default Linear, hint 4), `dynamics.tone` (default 35,
   hint 4) and `velocityRange` (0-48 dB, default 30, hint 1). The last three were in the
   DYNAMICS popover (CURVE, RANGE, TONE).
2. **DYNAMICS DSP.** Per voice, in `InstrumentEngine::applyDynamics` and the voice's velocity
   shaping (level range, tilt, attack bite, CHARACTER coupling through `dynamics.tone`). It is not
   part of the shared FX chain.
3. **The macro widget.** One of six `MacroKnob`s (`PluginEditor.cpp`, `macroInfo`). The row was
   LIFE, DYNAMICS, CHARACTER, MOVEMENT, ECHO, SPACE: ECHO, the sixth macro, arrived after this
   spec was written. DYNAMICS' identity colour was ochre `#B5843A`.
4. **Advanced.** `AdvancedPopup` (`ShapingPopups.cpp`): FINE, BEND, GLIDE; VOICES; PITCH
   CHARACTER; MPE and RESEED.
5. **The shared FX chain.** `PostProcessor::process`, per sample: the legacy shared
   REIMAGINED stage (0 with per-layer routing), the MOVEMENT bus, then ECHO and SPACE as
   parallel sends. VOLUME follows in the engine.
6. **CHARACTER.** A per-voice filter (`CharacterFilter`) with its own `character.drive`, a
   gentle saturation inside the filter, before the mix. It is left exactly as it was.
7. **State.** APVTS parameters plus properties. `setStateInformation` resets every parameter
   missing from the session to its default, so new parameters need no migration code.
8. **Oversampling.** None in the engine. `src/engine` is pure C++ by rule (no JUCE), so
   `juce::dsp::Oversampling` cannot be used there.
9. **Regression tests.** Golden renders (`regression`), Reimagined references, and the plugin's
   recall and migration tests. A new baseline was added for this change (section 6).

## 2. Signal chain

```
layers A/B/C (per voice: LIFE, Dynamics, CHARACTER filter + its drive, REIMAGINED, ADSR)
  -> mix (LEVEL, PAN)
  -> PostProcessor: [legacy REIMAGINED stage] -> DRIVE -> MOVEMENT bus -> ECHO and SPACE sends
  -> VOLUME
```

DRIVE is one new stage, after everything per voice and before the shared effects.
- MOVEMENT's chorus and tape modulate the driven sound, not the reverse, so there is no
  intermodulation between detuned copies.
- The repeats and the room are made from the saturated sound, so reverb tails are never
  distorted.
- CHARACTER keeps its position and its own drive. The two meet as gain staging: CHARACTER's drive
  is a gentle per-voice colour; DRIVE works on the mix.

## 3. DSP

`src/engine/DriveProcessor.{h,cpp}`, pure C++, real-time safe, deterministic.

**Common structure, per channel:**
1. A pre-emphasis pair of first-order shelves, low and high.
2. Up 4x (2x at 88.2 kHz and above, so the core always runs at 176-192 kHz).
3. The circuit's nonlinear core.
4. Down 4x.
5. The exact inverse de-emphasis shelves.
6. Amount-scaled top-end smoothing.
7. A 6 Hz DC blocker.
8. Static output compensation.

**The emphasis pair.** The two shelves are exact inverses (the de-emphasis section is the
pre-emphasis section's numerator and denominator swapped), so the clean sound passes flat.
They only decide how hard each region is driven:
- **Bass:** less of it goes into the nonlinear stages, so chords and low notes stay clean.
- **Treble (TONE):** driven harder, the treble saturates first and the harmonics come out darker
  (TONE towards 0). Driven less, the harmonics are brighter (TONE towards 1). The clean sound's
  own frequency response is never filtered.

**Staging.** Each stage has unity small-signal gain and runs in "driven units". DRIVE sets the
preamp gain g1, which rises with amount^1.2, so the first half of the knob is the nuanced
half. The second stages join from about 30 %. Below 8 % the stages ease out of their own
curvature, so a barely engaged DRIVE is transparent (flat within 0.03 dB from 1 kHz to
19 kHz, within 0.1 dB at 60 Hz).

**TUBE** (the default; preamp up to +34 dB):
- **Bass split.** A complementary split at 150 Hz (TPT second-order high-pass, low band = the
  rest, so the two sum exactly). The bass goes through its own gentle symmetric stage, so a
  low note cannot modulate the asymmetric stage above it. Without the split, intermodulation
  of 50 Hz + 2 kHz was -9 dB at full; with it, -28 dB.
- **Upper band, stage 1.** An asymmetric soft stage: a biased tanh-family [3/2] Pade
  saturator. Its bias follows the driven level (attack 20 ms, release 120 ms), as grid current
  shifts a real tube's operating point, so the clipping stays asymmetric however loud the note.
  The even harmonics remain: H2 -24 dB at 65 %.
- **Coupling.** A 5 Hz high-pass between the stages removes the asymmetry's DC.
- **Stage 2.** A symmetric soft stage that joins from 30 %, harder with BODY, for the rounded
  breakup.

**TAPE** (up to +22 dB):
- **The magnetising stage.** An algebraic saturator (x / sqrt(1 + x^2), which approaches its
  limit slowly, so the harmonics stay round) inside a lagging, mildly regenerative loop.
- **The lag.** 8-14 kHz by TONE, fading in with DRIVE: highs saturate and soften first.
- **Feedback.** Up to 0.4: memory, a hysteresis-like loop.
- **Transients.** A linked (stereo-coherent) fast/slow envelope pushes transients harder than
  the sustain. BODY sets how much.
- **Stage 2.** A gentle second stage for the "natural compression".
- No hiss, no wow or flutter: that is MOVEMENT's TAPE.

**CRUNCH** (up to +38 dB):
- **Preamp.** An asymmetric soft stage, its bias following the level a little.
- **Mid-forward knee.** A band around 1.1 kHz (TPT band-pass, scaled with DRIVE) pushed into a
  hard-knee stage, x / (1 + x^4)^(1/4): smooth but sharper than tanh.
- **Output.** A soft output limit.
- Stronger bass conditioning: -14 to -6 dB below 200 Hz.

**BODY.**
- **Dry share.** Low BODY keeps a little of the clean signal inside the oversampled domain:
  phase-coherent, no comb filtering. It counts only once the stages saturate.
- **Density.** High BODY sends more bass into the stages, adds second stage and, in TAPE, more
  transient push.
- **Not more drive.** The compensation table includes BODY, so BODY changes density, not level.

**Oversampling.**
- Polyphase IIR half-bands (two parallel chains of first-order allpasses; elliptic design after
  Valenzuela and Constantinides, as in Laurent de Soras' HIIR), designed in `prepare()`.
- Stage 1: 8 coefficients, 90 dB, flat to about 0.42 of the base rate.
- Stage 2: 4 coefficients, 80 dB.
- Minimum phase: no latency to report and about one sample of group delay, so the stage can
  engage and release without any host latency change.
- 4x was chosen: aliasing at 100 % on a 6.9 kHz tone is -111 dB (TUBE), -79 dB (TAPE) and -139 dB
  (CRUNCH). 2x would cost less but would leave the stage-2 images of hard CRUNCH closer to the
  audio band; 8x measured no further audible benefit and costs twice as much.
- A bug found on the way: two stateful decimator calls written as arguments of one function
  call ran in an unspecified order. They are two statements now, and a transparency test
  guards it.

**Output compensation.** Static: the saturator's ceiling plus a measured correction per
circuit, DRIVE (0-100 % in 10 % steps) and BODY (0 / 0.5 / 1, blended). It is measured with the
`[drive-measure]` test as the K-weighted RMS (BS.1770 weighting) of a vowel, saw and pluck chord
at the reference level (peaks about -12 dBFS):
- DRIVE then adds about +0.1 dB per 10 %, so +1.0 dB at 100 %, at every BODY.
- TONE moves it by at most +-0.7 dB.
- It is not an automatic gain, so it cannot pump. Like hardware, DRIVE reacts to how hot it is
  fed: a quiet single note is driven less than a full chord.

**Smoothing and switching.**
- Amount, TONE and BODY glide over about 30 ms at a 16-sample control rate.
- Engage and release fade over 10 ms. From silence, DRIVE starts at 0 and rises.
- A circuit change crossfades the new, freshly reset circuit in over 50 ms. Held notes keep
  playing.

## 4. Measurements (`[drive-measure]`, 48 kHz)

Harmonics of a 220 Hz tone at the reference level (dB relative to the fundamental):

| | H2 | H3 | H4 | H5 | H7 | H9 |
|---|---|---|---|---|---|---|
| TUBE 15 % | -37.6 | -44.3 | -76.7 | -90.5 | -133 | -161 |
| TUBE 40 % | -28.3 | -30.0 | -51.9 | -61.2 | -89 | -116 |
| TUBE 65 % | -24.2 | -17.2 | -31.8 | -35.2 | -46 | -59 |
| TUBE 100 % | -30.6 | -11.3 | -31.5 | -21.7 | -26 | -31 |
| TAPE 15 % | -58.4 | -35.4 | -84.6 | -66.2 | -88 | -96 |
| TAPE 65 % | -44.7 | -17.9 | -52.9 | -30.3 | -41 | -51 |
| TAPE 100 % | -51.8 | -13.4 | -54.1 | -20.4 | -26 | -31 |
| CRUNCH 15 % | -39.8 | -39.3 | -67.9 | -80.6 | -118 | -145 |
| CRUNCH 65 % | -41.1 | -15.7 | -41.5 | -21.3 | -26 | -30 |
| CRUNCH 100 % | -35.6 | -26.3 | -38.0 | -30.6 | -36 | -38 |

What the table says:
- At 10-25 % every circuit adds harmonics around -35 to -45 dB: richness without audible
  distortion.
- At 65 % they are clearly different:
  - TUBE is even plus odd, warm.
  - TAPE is odd harmonics falling away fast, smooth.
  - CRUNCH is dense upper harmonics, raw.

Intermodulation (50 Hz + 2 kHz at 4:1, sidebands relative to 2 kHz):

| | TUBE | TAPE | CRUNCH |
|---|---|---|---|
| 50 % | -34.5 dB | -34.8 dB | -27.5 dB |
| 100 % | -27.7 dB | -41.7 dB | -35.1 dB |

Transients (a pluck's crest factor, peak/RMS in dB, at 0 / 40 / 65 / 100 %):

| | 0 % | 40 % | 65 % | 100 % |
|---|---|---|---|---|
| TUBE | 19.4 | 18.6 | 15.3 | 11.6 |
| TAPE | 19.4 | 14.0 | 10.6 | 7.6 |
| CRUNCH | 19.4 | 16.1 | 9.9 | 13.1 |

TAPE rounds transients most; CRUNCH stays the most articulate at full.

CPU:
- The stage alone: about 2 % of one core of this cloud VM, stereo, at 44.1, 48 and 96 kHz,
  whatever is playing.
- In the plugin (`[cpu-profile]`): 16 voices with DRIVE 70 % cost +2 to +4 % of the audio budget
  over DRIVE off, at every rate and block size (64-512).
- The full scene (three granular layers at REIMAGINED 100 %, DRIVE, MOVEMENT chorus and SPACE)
  runs at 47 % mean at 48 kHz / 128. Worst-block spikes are the VM's scheduler: they occur
  equally with DRIVE off.

## 5. GUI

- **The macro.** The second macro is DRIVE, with DYNAMICS' ochre (`#B5843A`, the approved
  palette's value). Its position, size, arc and LED are unchanged, and the rest of the panel is
  untouched. ECHO stays.
- **The popover.** The shared compact popover anchored above DRIVE:
  - **Header:** DRIVE, with TUBE / TAPE / CRUNCH as its quiet selector.
  - **Picture:** the circuit's live transfer curve, from `DriveProcessor::transfer` (the DSP's own
    shaping functions in steady state). A faint dashed diagonal shows the clean sound; at 0 %
    the picture says CLEAN. TAPE adds a faint dashed curve for a transient, since a stage with
    memory has no single curve.
  - **Knobs:** TONE and BODY. There is no second amount: the macro is the amount.
- **Advanced.** Advanced has a DYNAMICS section under VOICES and PITCH CHARACTER: SOFT / LINEAR
  / HARD, then AMOUNT (`dynamics`), RANGE (`velocityRange`) and TONE (`dynamics.tone`) on the
  existing parameters. Its subtitle reads "TUNING, VOICES & DYNAMICS".
- **MIDI.** CC 27 is DRIVE's. CC 21 still moves Dynamics, so existing MIDI mappings keep doing
  what they did.

## 6. Compatibility

**Parameter IDs.**
- DYNAMICS keeps `dynamics`, `dynamics.curve`, `dynamics.tone` and `velocityRange`, with the same
  ranges, defaults, version hints and DSP. Host automation still lands.
- DRIVE has new IDs: `drive` (the amount, following the repo's convention for macros, like
  `echo`), `drive.mode`, `drive.tone` and `drive.body`, version hint 14.

**Old sessions.** They contain no `drive.*`, so DRIVE opens at 0, which bypasses it, even in an
instance where DRIVE had been moved (tested).

**Baseline null test** (`[.][drive-baseline]`):
- Nine sessions were made and rendered by the build before DRIVE (commit `960cf56`): One Shot,
  Granular, two layers, three layers, heavy REIMAGINED (TAPE FRAME 90 %), DYNAMICS automated
  during the render, CHARACTER drive with resonance, SHAPER, SPACE + ECHO.
- Their saved states were then loaded by the new build and rendered again.
- All nine are bit-identical: largest difference 0.
- Rendering from the same states twice in the old build was also bit-identical, so the test is
  deterministic.

**Defaults.** New patches: DRIVE 0 %, TUBE, TONE 50 %, BODY 50 %. Starting states and INIT leave
DRIVE at 0.

## 7. Tests

- **Unit** (`[unit][drive]`): bypass exact; the half-band design; transparency when barely
  engaged; silence in gives silence out; a +18 dBFS square stays bounded and finite; no DC on a
  driven E1 (41.2 Hz) and its fundamental kept within +-6 dB; aliasing below -50 dB at 100 %;
  sweeps, fast random automation and circuit switching without clicks; stereo identity, no
  crosstalk; determinism; level and harmonics the same at 44.1, 48 and 96 kHz; the transfer curve
  monotonic and bending.
- **Plugin** (`[plugin][drive]`): recall; old sessions open DRIVE off; DYNAMICS' IDs and
  defaults; DRIVE changes the sound in every circuit, bounded; CC 27; DRIVE 0 untouched; DYNAMICS
  still shapes soft notes.
- **Hidden:** `[drive-measure]` (the tables above), `[drive-baseline]` (the null test) and
  `[cpu-profile]` (now with DRIVE at every rate and block).
- **Snapshots:** the DRIVE popover in each circuit, and Advanced.

## 8. Known limitations

- **Voiced by measurement, not by ear.** I can't listen. The voicing was tuned against harmonic
  profiles, intermodulation, transient and loudness measurements. The listening tests the spec
  asks for (sections 47-51: the corpus sources at low, medium and full DRIVE, blind mode
  comparison at 65 %) are still to be done by you on the build. The values most likely to want
  your ear:
  - the drive gain per circuit (`maxDb`);
  - TUBE's bias drift;
  - TAPE's feedback and lag;
  - CRUNCH's mid emphasis;
  - BODY's dry share.

  All of these live in `DriveProcessor::voice`.
- **Level-dependent by design.** The voicing assumes program peaks of about -12 dBFS, OSP's
  level for a chord at VOLUME 0 dB. Very quiet material is driven less; there is no input trim.
- **Six macros, not five.** The spec lists five macros; the panel has six because ECHO came
  after it. ECHO is kept, as the spec asks to leave everything else unchanged.
- **Advanced is taller,** by one row of three knobs and a selector.
- **Not tested in a DAW.** No DAW was run. auval, the plugin tests and CI cover loading and
  processing only.
