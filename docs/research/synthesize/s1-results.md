# SYNTHESIZE S1 — results, measurements and the listening guide

Phase S1: one analog-style oscillator per sounding voice, blended with the recording by one
SYNTHESIZE amount per source (A, B, C). Design and the PHRASE comparison: `s1-audit.md`.
This build is for musical evaluation; nothing here claims a musical verdict. That is yours.

## 1. What was built

| | |
|---|---|
| Oscillator | PolyBLEP saw, one per voice, start phase from the note's seed (deterministic) |
| Tone | fixed 12 dB/oct state-variable low-pass, Q 0.6, cutoff 4-8 x the pitch with velocity (250 Hz-12 kHz) |
| Envelope | A (default): about 12 ms attack, full while held (key or pedal), about 140 ms to -40 dB after release; then the voice's own ADSR, velocity and steal fade. B (internal, `synthEnvelope: follow`): the recording's contour first, settling into sustain over 0.4 s |
| Pitch | the played key x bend, fine, TUNE, LIFE, drift, routed cents, glide; never the recording's root or a root correction. Granular: the layer's TUNE, not grain scatter or KEY DNA offsets |
| Blend | hybrid: the sample stays whole up to 35 %; the oscillator rises as a^1.5; both power-normalised; at 100 % only the oscillator |
| Level | each voice's oscillator calibrated to its recording's typical level: halfway in dB between the loudest 20 ms and the body's median, at most 6 dB under the loudest |
| Where | mixed in the voice after the source read, before the voice's shelves, CHARACTER, DAMPEN, ADSR and pan; then the layer's EQ, the mix and the effects as always |
| At 0 % | not computed at all: the voice is bit-identical to before |
| Lifetime | a One Shot that runs out while its key is held keeps its voice for the oscillator; the voice ends with the oscillator's release. At 0 % voices end exactly as before |
| Control | SYNTH, a slim graphite fader in each source well's top-right corner; host parameter `layerA/B/C.synthesize` 0-100 % (version hint 22) |

## 2. The GUI decision

A sixth knob after REIMAGINED was tried first (screenshots kept locally). At three sources the
REIMAGINED and SYNTHESIZE names ran together; at two the knob ran into the modifiers' divider.
Shrinking the five established knobs was ruled out by the brief, so the control moved into
the well's ruler band, where there was unused room in all three layouts:

- **Inside every display:** top-right of each source's waveform display, in the ruler band.
  The ruler's time labels keep clear of it. It is hidden while EQ, NOTES or a product's
  editor covers the display.
- **Look:** the KEY DNA key's hairline cell on graphite, reading "SYNTH 0 %". Above 0 %, a
  small light and a thin bar in the source's colour.
- **Use:** drag (up or right) or turn the wheel; Alt for fine; double-click returns to 0.
  The tooltip says what it does.
- **OSP only:** sister products built on the shell show nothing (`InstrumentHost::hasSynthesize`).

Screenshots: `[refine-shots]` 06-synth-1L / 2L / 3L (1x and 2x).

## 3. Loudness (the listening matrix, whole-render RMS against 0 %)

Renders through the research renderer (engine C, levelled sources, default macros). Each
figure is the whole render's RMS against the same render at 0 %. Decaying material therefore
gains where the oscillator sustains past the decay: by design, the pluck's held tone.

| Scene | 10 % | 25 % | 50 % | 75 % | 100 % | loudest peak |
|---|---|---|---|---|---|---|
| A vocal one-shot (sung E5, a phrase) | +0.0 | +0.1 | +0.3 | +0.4 | -0.1 | -22.7 dBFS |
| B bowed nyckelharpa | +0.0 | +0.1 | +0.6 | +2.4 | +3.7 | -25.1 |
| C bowed Atlas, melody | +0.0 | +0.1 | +0.6 | +2.4 | +3.6 | -26.9 |
| C bowed Atlas, chords | +0.0 | +0.1 | +0.7 | +2.5 | +3.7 | -22.4 |
| D pluck (LOOP off) | +0.0 | +0.1 | +0.9 | +3.0 | +4.4 | -22.6 |
| E granular pad + KEY DNA | -0.0 | -0.1 | +0.0 | +0.9 | +2.0 | -20.1 |
| F eight-note chord | -0.0 | -0.0 | -0.1 | -0.3 | -0.2 | -12.9 |
| G register sweep (C0-C4) | +0.0 | +0.0 | +0.1 | +0.6 | +1.0 | -18.5 |

1-25 % moves the level by at most 0.1 dB everywhere (the brief's "minimal perceived level
change"). Nothing clips.

The first calibration, to the loudest 20 ms, gave +8 to +10 dB at 100 % on bowed, plucked and
Atlas material. That was fixed twice before this table:
- calibrate to the recording's typical level instead;
- no −40 dBFS floor on that level (the nyckelharpa recording is quieter than that before
  levelling).

### Blend laws compared (whole-render dB against 0 %)

| | 25 % | 50 % | 75 % |
|---|---|---|---|
| vocal: hybrid / equal-power / additive | +0.1 / +0.4 / +0.5 | +0.3 / +0.4 / +1.4 | +0.4 / +0.1 / -0.6 |
| nyckelharpa | +0.1 / +0.8 / +0.6 | +0.6 / +2.2 / +2.0 | +2.4 / +3.3 / +1.9 |
| pluck | +0.1 / +1.0 / +0.7 | +0.9 / +2.8 / +2.3 | +3.0 / +4.0 / +2.7 |

Equal power hands half the sound to the oscillator by 25 % (sin 22.5° = -8 dB) and halves the
recording by 50 %. The hybrid keeps the recording whole until 35 %, so the lower third of the
control adds body rather than replacing anything. That is the reason for the default, but the
renders `law-*` are there for your ears. Envelope B (`env-*-follow-050`) measures within
0.3 dB of A; whether it sounds better is a listening question.

## 4. Tuning and aliasing

- Oscillator alone: within 0.05 cents at 55, 220, 440 and 1760 Hz. Aliasing at C8 below
  -40 dB ([synth]).
- In the instrument at 100 % (G register sweep, C0-C4, LIFE at its default): measured
  +2.8, 0.0, -2.0, -2.8, -0.8 cents, which is LIFE's pitch life.
- Bend, TUNE and Granular TUNE are followed. A root correction is not applied a second
  time ([synth]).

## 5. CPU

See §8 (measured after the test suites, on an idle machine).

## 6. Tests

- Engine `[synth]` (6 cases):
  - the blend laws;
  - tuning, aliasing and the envelope;
  - 0 % bit-identical;
  - the played pitch, bend, root correction and TUNE;
  - a pluck held by its oscillator, then released with no voice left;
  - block size, automation 0→1→0 on an 8-note chord with no allocation and no clipping;
  - loudness within 3 dB at 10-100 %.
- Plugin `[plugin][synthesize]` (7 cases):
  - an appended parameter, 0 % default; saved and reopened; an older session opens it off;
  - 0 % identical in One Shot and Granular, also after turning it back down;
  - mute and solo silence a layer's oscillator; each layer is its own;
  - pedal, stealing (60 notes), all notes off, repeated notes and all sound off leave no
    voice;
  - a One Shot held past its end keeps sounding, and its release ends it;
  - an Atlas key left silent (FILL off) stays silent, a mapped one plays;
  - automation on held notes is deterministic and allocates nothing.
- Main ctest (goldens, legacy and Phase 0-4B renders): unchanged.
- Parameter-schema golden: three appended lines.

## 7. Listening guide (renders are local, in the session scratchpad: `synth-listen/`)

Each file is `<scene>-<amount>.wav`, made from your recordings, so they stay out of the
repository. Suggested order:

1. **A vocal**, 0 → 25 → 50: does 25 % sound like the same voice with more body? Where does
   it stop being a voice?
2. **D pluck**, 0 → 50 → 100: the natural attack with a synthetic sustain.
   - Does the oscillator enter smoothly?
   - Is the held tone a feature or a distraction?
3. **B nyckelharpa / C Atlas**: does the bow noise survive at 50 %? Does each Atlas note's
   oscillator track the melody and the chords?
4. **E granular + KEY DNA**: a stable analog floor under the cloud, or a second sound?
5. **F eight-note chord** at 75-100 %: low-end build-up and beating.
6. **G register**: tuning and brightness across four octaves.
7. **`law-*`**: the three blend laws at 25/50/75 on vocal, nyckelharpa and pluck.
   **`env-*-follow-050`**: envelope B.

In the plugin: load anything, raise SYNTH on a card, play. Try Granular, an Atlas
instrument (MAP NOTES), chords and the pedal. Save, reopen.

## 8. Known limitations

- One fixed saw and one fixed tone: no TONE, no shape, by the brief. Bright, high
  recordings may want a brighter oscillator, and dark ones a darker one.
- Calibration is per recording (typical level), not per moment: a recording with very
  uneven notes gets one oscillator level for all of them.
- At 100 % in Granular the cloud's own pitch scatter and KEY DNA TUNE offsets are not
  followed (on purpose): the oscillator plays the key.
- REIMAGINED modes: the oscillator is added before the voice's filters, beside the mode's
  output. It is not fed into the mode engines.
- Envelope B and the alternative blend laws are reachable only from research configs.
- The SYNTH fader is not a modulation target yet. The route destination list is frozen, and
  adding it means extending `destExt`.
- Not verified on a Mac by ear: Apple Silicon build from CI, untested in a DAW.
