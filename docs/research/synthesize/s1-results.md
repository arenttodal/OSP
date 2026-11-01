# SYNTHESIZE S1 — results, measurements and the listening guide

Phase S1: one analog-style oscillator per sounding voice, blended with the recording by one
SYNTHESIZE amount per source (A, B, C). Design and the PHRASE comparison: `s1-audit.md`.
This build is for musical evaluation; nothing here claims a musical verdict. That is yours.

## 0. Installing the development build (macOS, Apple Silicon and Intel)

Every push to `claude/charming-lovelace-n25fte` builds it in CI (macOS 15, Apple Silicon
runner, universal binaries, the AU validated with `auval`). Download the **OSP-macOS-test**
artifact of the latest green "CI" run, unzip it, double-click `install.command` (right-click →
Open the first time; it is unsigned and the installer removes the quarantine), then:

| | installed to |
|---|---|
| AU | `~/Library/Audio/Plug-Ins/Components/OSP.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/OSP.vst3` |
| Standalone | `~/Applications/OSP.app` |

Ableton Live: Settings → Plug-Ins → enable "Use Audio Units v2" and/or "VST3 Plug-In System
Folders", then **Rescan**. OSP appears under Plug-Ins → Audio Units → OSP, or under VST3 → OSP.
Uninstall: delete those three files.

Building it yourself instead: `cmake -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
-DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin`. The bundles are then in
`build-plugin/apps/plugin/OSP_Plugin_artefacts/Release/{AU,VST3,Standalone}`.

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

Measured on the cloud build machine (Linux, x86-64, shared, 48 kHz / 256-sample blocks, notes
held, median of 5 runs). Apple Silicon will be faster in absolute terms; the ratios are the
point. Re-run with `osp_tests "[synthbench]"` and `osp_plugin_tests "[synthbench]"`.

**The oscillator itself** (saw, low-pass, envelope, control update): **4.9 ns per sample =
0.024 % of one core per voice**, so 32 voices add about 0.8 %. At 0 % it does not run.

**The engine** (no effects), % of one core, off vs 100 % (two runs; the shared VM varies by
±20 %):

| | off | 100 % | added |
|---|---|---|---|
| 1 voice | 0.31-0.33 | 0.34-0.35 | +8 % |
| 8 voices | 2.0-3.0 | 2.5-3.4 | +15-30 % |
| 16 voices | 5.1-7.9 | 6.7-8.9 | +13-32 % |
| 32 voices | 12.0-15.1 | 13.0-13.4 | -14 to +11 % (noise) |
| 8 notes x 3 layers | 6.0-7.5 | 7.7-8.1 | +8-29 % |
| 8 voices Granular | 2.0-2.5 | 2.2-2.7 | +7-13 % |
| 8 voices Granular + KEY DNA | 2.3-3.6 | 2.5-3.8 | +6-11 % |
| 8 notes x 3 layers Granular + KEY DNA | 5.8-6.4 | 6.7-6.8 | +5-14 % |

**The whole plug-in** (default patch: macros, CHARACTER, SPACE and the rest as a new instance
has them): 1 note 1.2 → 1.9 %; 8 notes 4.2 → 4.8 %; 16 notes 12.9 → 10.1 %; 32 notes 24.5 →
24.4 %; 8 notes x 3 layers 18.9 → 19.4 %; 16 notes Granular 5.9 → 6.0 %; 8 x 3 layers Granular
8.0 → 9.5 %. The differences are within the machine's noise: the effects and the sample reads
dominate. Granular budgets and KEY DNA are untouched (the oscillator is per voice, not per
grain).

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

## 7. Auditioning the alternatives in the plug-in (development only)

**Right-click (or Ctrl-click) any SYNTH fader** → *SYNTHESIZE audition (development, not saved)*:

- **Blend:** Hybrid (default) · Equal power (PHRASE's) · Additive
- **Envelope:** Gate (default) · Follow the recording (envelope B)
- **Back to the defaults**

The choice applies to every source, for notes played afterwards. While it is not the
default, a small amber corner marks every SYNTH fader. It is **never saved**: sessions and
presets do not store it, and every new instance starts at Hybrid + Gate, so nothing you save
depends on it. At 0 % it changes nothing (tested). `[plugin][synthesize]` covers it.

## 8. Problems found (for your listening)

1. **Pitch: the oscillator is in equal temperament, the recording is not.** On the held notes:
   - Sung E5: the recording is +5.6 cents with ±20 cents of vibrato (7.9 c spread); the
     oscillator holds +1.3 c (LIFE).
   - Nyckelharpa: −4.3 c on its first note; −11 c on average over the bow, ranging −77 to
     +18 c.
   - Pluck: −5.9 c against +2.0 c.

   At 25-75 % the two therefore **beat slowly**: 0.4-1.7 Hz at the fundamental, n times that
   at the n-th harmonic. It moves with the vibrato. This is the most likely reason the
   oscillator could read as "a synth underneath" rather than the sample itself changing.
   Not changed in S1. The S2 candidate is to let the oscillator follow the recording's own
   pitch: the analysis already has a per-frame pitch track, as PHRASE's oscillator followed
   the region. Listen for it in A, B and D at 50 %.
2. **Release: at high amounts the release is shorter than the instrument's R.** The oscillator
   has its own release (about 140 ms to −40 dB) inside the voice's ADSR. At 100 %, where the
   recording is silent, a note therefore stops in about 0.2 s even with R at 700 ms (the
   default). Below 50 % the recording carries the release and this is barely audible. It is
   a one-line change if you prefer the oscillator to release with R; deliberately not changed
   before your listening.
3. **Beating in chords (F):** at 75-100 % an eight-note chord of saws beats as any saw chord
   does (equal temperament); low notes add level (C0 in F). The whole-render level stays
   within 0.3 dB.
4. **High register:** the oscillator's worst alias, relative to its fundamental:
   - −87 dB at C5, −69 dB at C6, −55 dB at C7, −61 dB at C8, −53 dB at C9;
   - the low-pass caps the tone at 12 kHz.

   Unlikely to be heard under a recording; possibly audible as faint inharmonic tones on
   pure-oscillator (100 %) notes above C7.
5. **Envelope transitions:**
   - Attack: 12 ms, so on plucks the recording's attack leads and the oscillator swells in
     under it, as intended.
   - Turning SYNTH while holding: smoothed over 20 ms, no clicks (tested).
   - Envelope B (follow) measures within 0.3 dB of A.
6. **Voice stealing:** a stolen voice fades the sample and the oscillator together (the
   voice's own fast fade); 60-note storms, pedal, all-notes-off and panic leave nothing
   sounding (tested).
7. **Loudness:** 1-25 % stays within 0.1 dB. At 100 % decaying sources rise by up to +4.4 dB
   over a whole phrase, because the oscillator sustains where the recording decays. The
   eight-note chord stays within 0.3 dB. No clipping.

## 9. Listening guide (renders are local, in the session scratchpad: `synth-review/`)

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

## 10. Known limitations

- One fixed saw and one fixed tone: no TONE, no shape, by the brief. Bright, high
  recordings may want a brighter oscillator, and dark ones a darker one.
- Calibration is per recording (typical level), not per moment: a recording with very
  uneven notes gets one oscillator level for all of them.
- At 100 % in Granular the cloud's own pitch scatter and KEY DNA TUNE offsets are not
  followed (on purpose): the oscillator plays the key.
- REIMAGINED modes: the oscillator is added before the voice's filters, beside the mode's
  output. It is not fed into the mode engines.
- Envelope B and the alternative blend laws: the development audition menu (§7) and
  research configs; not in presets.
- The SYNTH fader is not a modulation target yet. The route destination list is frozen, and
  adding it means extending `destExt`.
- Not verified on a Mac by ear: Apple Silicon build from CI, untested in a DAW.
