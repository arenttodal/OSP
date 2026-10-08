# REIMAGINED modes

Each layer's REIMAGINED is now a choice of five ways of reinterpreting its recording. The
knob on the card says how far; the mode says which way. Older patches play
KALEIDOSCOPE, the original algorithm, unchanged to the sample.

| Mode | Interpretation | Settings (defaults) |
|---|---|---|
| KALEIDOSCOPE | Refraction | FOCUS 50, SPREAD 50 |
| TAPE FRAME | Mechanical memory | AGE 35, STABILITY 25, FRAME CLASSIC |
| TOYBOX | Primitive digital mutation | MOTION 30, DIGITAL 40, PLAY TURN |
| MOSAIC | Harmonic reconstruction | DETAIL 60, MOTION 45, MODEL TEXTURED |
| MIRAGE | Early-digital weight | CLOCK 45, FILTER 50, TONE DARK |

## Architecture

- **Engine interface.** `ReimaginedVoiceEngine` (`src/engine/ReimaginedEngine.h`) is the
  one contract:
  - `prepare` (off the audio thread) and `start` (returns false when the mode cannot play
    this note: the voice then plays the recording).
  - `release`, and `control` (control rate, every 32 samples: the live amount and settings).
  - `stepFactor` (the engine's own pitch movement: wow, self-modulation).
  - `wantsDryRead` (it needs the voice's plain read).
  - `render` (up to 32 samples), `sourcePosition` (display), `finished`.

  There are four implementations: `TapeFrameEngine`, `ToyboxEngine`, `MosaicEngine` and
  `MirageEngine`.
- **KALEIDOSCOPE.** It stays the voice's own native path: the per-voice part shaped at
  note-on, plus the layer's `ReimaginedStage`. Its amount mapping moved unchanged into
  `KaleidoscopeEngine.h`. It was not put behind the interface because the risk of a sonic
  change outweighed any gain.
- **Per-layer state.** `LayerSettings::reimaginedSettings` (`ReimaginedSettings`) holds the
  mode plus every mode's parameters, so switching keeps them all. The amount stays
  `LayerSettings::reimagined`.
- **Voice model.**
  - **Allocation.** Every voice holds one instance of each engine inline: about 7.8 KB of
    its 16.6 KB, 208 voice slots, about 3.4 MB in all, allocated once.
  - **Engine choice.** At note-on the layer's mode picks the engine, if its amount is above
    0; that note keeps it.
  - **Render path.** Mode voices render through `InstrumentVoice::renderMode`, then the
    same envelope (ADSR, note-off), shelves, CHARACTER filter, level and pan as every note.
    The KALEIDOSCOPE render loop was not touched.
  - **Pitch source.** TAPE FRAME, TOYBOX and MIRAGE read the original recording themselves;
    they are their own pitch character.
  - **MOSAIC.** It takes the voice's plain read (continuation included) while it
    crossfades, and stops asking for it once it has fully taken over.
  - **Granular.** In Granular mode the grains are every engine's input.
- **Signal chain.** Source → One Shot / Granular → the mode engine (or KALEIDOSCOPE's
  voice part) → envelope, CHARACTER → the layer's ReimaginedStage (KALEIDOSCOPE layers
  only) → LEVEL / PAN → mix → MOVEMENT → SPACE → VOLUME.
  - **Stage while switching.** Switching a layer away from KALEIDOSCOPE fades its stage out
    (about 10 ms smoothing, its tail rings down).
  - **Legacy routing.** In legacy routing the shared stage counts only KALEIDOSCOPE layers,
    which is unchanged when all of them are.
- **Analysis cache.** `ReimaginedAnalysis` (`src/model/`, schema 1) is built by
  `analyseReimagined` on the loader's worker thread, together with the continuation
  model (stage 2), and lives in the immutable `InstrumentModel`, shared by every voice.
  - **What it reuses.** The pitch track, the envelope, and the continuation's sustain
    region and matched jumps.
  - **What it holds.** No copy of the audio:
    - **TAPE FRAME:** a splice map, as references into the recording.
    - **MOSAIC:** 16–32 harmonic frames of 48 partials, the inharmonicity and 6 residual
      noise bands.
  - **Size and time.** About 8.6 KB and 1.4 ms for a 3 s recording.
  - **Caching.** Each source is analysed once: changing mode never analyses again. A new
    recording is a new model with new data. A root change only moves the pitch mapping.
  - **Readiness.** Before stage 2 (`reimagined == nullptr`, the popover shows
    "ANALYZING…") and when a part fails, those notes play the plain recording. Notes never
    go silent.

## Legacy compatibility

- **Null test (RI-00 / RI-01).** I rendered 18 reference scenes with the build before any
  mode code (tag `ri-00-baseline`) and again with the final engine. All 18 are
  sample-identical, max |before − after| = 0:
  - The 12 legacy scenes: 0–100 %, granular, two and three layers, mixed modes, shaped,
    automated.
  - 6 new per-layer KALEIDOSCOPE scenes: 25/50/75/100 %, two layers at 30/80 %, three
    layers granular at 60 %.

  The regression test `plugin: sessions from before per-layer Reimagined render as they
  did` checks all 18 (hash and metrics). The core goldens are unchanged.
- **FOCUS and SPREAD.** They are exactly neutral at 50 %: a branch, not arithmetic, keeps
  the original expressions bit for bit.
- **State version 9.**
  - **Older states.** A state below 9 has no mode parameters. It opens with every layer on
    KALEIDOSCOPE and every mode setting at its default, explicitly and whatever the
    instance had before.
  - **Kept as they were.** Amounts, routing (legacy or per layer), IDs and preset metadata.
  - **Files.** Files are never rewritten unless resaved. The factory starting states are
    unchanged.
- **Parameter IDs** (version hint 11, 15 per layer):
  - Pattern: `layerA.reimagined.mode`, `layerA.reimagined.kaleidoscope.focus` /
    `.spread`, `.tapeFrame.age` / `.stability` / `.frame`, `.toybox.motion` / `.digital` /
    `.play`, `.mosaic.detail` / `.motion` / `.model`, `.mirage.clock` / `.filter` / `.tone`.
    The same for `layerB` and `layerC`.
  - **Naming.** The spec suggested `engine.0.…`; these follow the project's existing
    `layerX.name` convention.
  - **Existing IDs.** No existing ID changed.
  - **Conversion.** A gesture on any of these converts a legacy patch to per-layer
    routing, as the amount does. Automation playback does not.

## DSP per mode

The amount is the main knob, 0–100 %. Each mode maps it through its own curves (smoothstep
ramps), never linearly. At 0 % no engine runs, so the note is exactly the original path.

### TAPE FRAME

- **Analysis.** The tape is the onset, then the body lengthened by its own matched loops.
  - **The walk.** A fixed walk over the continuation's jumps (the same tape for every
    note), never repeating the last three. Each splice gets ±0.35 dB of its own.
  - **Long recordings.** The recording is the tape. When the stable body starts late (a long
    evolving intro), the onset is spliced onto the body by correlation instead of always
    cropping from the start.
  - **Decaying sounds.** Their natural length.
- **Playback.**
  - **Tape speed.** It is the note's pitch, so low notes are slower, longer and darker.
  - **The read.** Sinc reads with correlation-aware splice crossfades.
  - **The end of the frame.** FRAME is 3.6 / 7.6 / 11.4 s of tape at the root. With LOOP
    on, near its end the tape is rewound to the body for another pass, each pass a little
    more worn (more at higher amounts, floored at -6 dB), so a held note sustains. With
    LOOP off the recording is read straight through once, to its own end. Reversed, the
    tape plays backwards from its end and, held, rewinds at the body's start.
  - **Each pass.** Every note is a different pass, seeded by the note's own (LIFE) seed:
    speed, start, wow and flutter phase, tone and gain.
  - **AGE.** A bandwidth (two poles) that follows tape speed and touch and wears slowly,
    plus tanh saturation and a soft limit on loud passages.
  - **STABILITY.** Wow up to ±14 cents, flutter, and the pass-to-pass variation.
  - **Ghost passes.** Two passes 30–90 ms behind, ±3–7 cents off speed, darker, at −14 and
    −19 dB, one to each side.
  - **Granular.** It ages and moves the grains.
- **Amount curves.**
  - Identity first: 35 % of it by 25 %, all of it by 60 %.
  - Pass wear from 25 to 60 % (up to -1.4 dB per pass).
  - Mechanics 30–85 %.
  - Ghosts 55–95 %.

### TOYBOX

- **Memory.**
  - **The stored sound.** Sampled at an internal rate through a crude averaging anti-alias
    filter, from transparent down to about 7 kHz.
  - **Resolution.** Stored at 16 down to 8 bits.
  - **The DAC.** A fixed-rate DAC with linear interpolation, moving towards drop-sample.
    Transposition therefore aliases the way small samplers do. A gentle post filter
    leaves some sparkle.
- **Heads.**
  - **FWD.** Plays through and loops a sustaining body.
  - **TURN.** A pendulum whose region walks forward (forward legs 1.3× longer).
  - **CHAOS.** Irregular legs, skips and occasional jumps, always on the recording's own
    pitch.
  - **U-turns.** A turn waits for the next extremum of the waveform under the head, so it
    needs no crossfade. A crossfade between a forward and a backward read of the same
    point cancels: I found and fixed exactly that. Jumps use equal-power crossfades.
- **Memory taps and self-modulation.**
  - **Taps.** Two taps 45–150 ms behind the head (bounded, no feedback loop).
  - **Self-modulation.** The sound's fast envelope against its slow average bends the rate
    (±22 cents) and the level instead of an LFO.
- **Amount curves.**
  - The digital character comes first: 60 % of it by 30 %.
  - Memory taps 25–55 %.
  - Turning 25–75 %.
  - Self-modulation 50–90 %.
  - Velocity nudges DIGITAL slightly.

### MOSAIC

- **Analysis.** 16–32 Hann-windowed frames (4.5 periods, 2k–16k FFT).
  - **Partials.** Each frame's own fundamental (from the pitch track); 48 partials by
    parabolic peak; inharmonicity B fitted on strong upper partials (median over frames).
  - **Residual.** The noise between the partials, as variance in 6 bands.
  - **Frames.** A stable frame (the strongest in the sustain region) and a body frame.
- **Playback.**
  - **Pitch.** Additive partials at the note's fundamental times each frame's pattern
    (B kept, ×0.3 in PURE). Because time is independent of pitch, the timbre holds far
    from the root.
  - **Implementation.** Partials are phasors renormalised at control rate, stored as odd
    and even halves so each is one vectorised loop. They fade before 0.45 × the sample
    rate, so nothing aliases.
  - **Residual.** Seeded noise through 6 normalised band-passes, following the recording's
    time on decaying sounds.
  - **Frames.** Interpolated continuously, never stepped.
  - **MOTION.** From one stable frame to a ping-pong through the body. Decaying sounds keep
    their decay.
  - **DETAIL.** 8–48 partials, and spectral smoothing at low settings.
  - **MODEL.** TEXTURED keeps the residual, the inharmonicity and a ±1.5 cent partial
    detune.
  - **Velocity.** Leans the partials and the residual.
  - **The attack.** The recording's own attack stays at the front (35–120 ms).
- **Amount curves.**
  - Crossfade into the reconstruction: 60 % of it by 25 %, fully by 60 %.
  - Travel through frames grows over 55–85 %.
  - 85–100 % adds a slowly moving spectral emphasis, on pitch.
- **Fallback.** Unpitched sources (noise, a few plucks with no stable pitch) play the
  recording. That is tested; the popover says so.

### MIRAGE

- **Memory.** Reduced rate (down to about 9 kHz) and resolution (16 down to 8.5 bits, a
  little coarser on soft passages).
- **Variable clock.** It is played by zero-order hold, so the clock follows the read: low
  notes get slow, stair-stepped grit and high notes skip and fold. Below a quarter of
  CLOCK the hold blends from interpolated, so low amounts stay clean.
- **Loop points.** Fixed loop points (the best continuation loop) with a correlation-aware
  crossfade.
- **Filter.** The CHARACTER LP24 ladder (nonlinear feedback, drive).
  - **Keytracking.** It follows the note's frequency.
  - **Movement.** It opens with touch and with a 0.4–1.2 s filter envelope, and wanders
    ±0.08 octave.
  - **FILTER.** Lower and more resonant, with more drive; resonance is bounded at 0.8.
  - **TONE.** OPEN raises it by 1.6 octaves.
- **Amount curves.**
  - Clocking first: 65 % of it by 35 %.
  - Filter identity 15–70 %.
  - Drive and resonance weight 60–95 %.

## Performance

**CPU.** 48 kHz, 128-sample blocks, 16 musical voices held (`[reimagined-cpu]`), on this
container (shared cloud x86, not Apple Silicon). The figure is the share of one core in
real time:

| Case | CPU |
|---|---|
| 1 layer KALEIDOSCOPE 100 % | 36–45 % |
| 1 layer TAPE FRAME 100 % | 24–30 % (12 voices still sounding at 4 s: high notes ran out of tape) |
| 1 layer TOYBOX | 14–16 % |
| 1 layer MOSAIC | 17–20 % |
| 1 layer MIRAGE | 13–17 % |
| A MOSAIC + B TAPE FRAME + C TOYBOX | 58 % (48 voice instances) |
| 3 layers MOSAIC (worst case) | 60 % |
| Granular + MOSAIC / TAPE FRAME / KALEIDOSCOPE | 22 % / 14 % / 16 % |

- **Comparison with KALEIDOSCOPE.** Every new mode costs less than KALEIDOSCOPE at 100 %,
  which runs a doubling head, grains and a resonator bank.
- **The MOSAIC fix.** At first, 3 × MOSAIC cost 102 %. Vectorised partial halves, skipping
  silent partials, and dropping the plain read once rebuilt brought it to 60 %, with no
  change to the sound's design.
- **Memory.** Per voice slot: TAPE FRAME 552 B, TOYBOX 4.6 KB (its tap history), MOSAIC
  2.2 KB, MIRAGE 568 B. Per recording: about 8.6 KB of analysis. Nothing duplicates audio.

## UI

- **The door.** Click a card's **REIMAGINED** name. It darkens and underlines on hover;
  open, the line is the aurora at the layer's amount.
- **The popover.** A compact popover, 270 × 188 (plus shadow), unfolds above the name, in
  the same shell as the macro popovers.
  - **Mode chooser.** The mode name at the top right, "KALEIDOSCOPE ▾"; click it for the
    list.
  - **Picture.** The mode's picture in the graphite well.
  - **Settings.** Two or three settings: continuous ones are small knobs whose arc is the
    aurora at the current amount (live); stepped ones are text selectors (FRAME, PLAY,
    MODEL, TONE).
  - **Closing.** Escape, a click elsewhere, or the name again.
- **Pictures.** Each mode's picture is drawn from this layer's data and settings:
  - **KALEIDOSCOPE:** the recording's form refracted into 3–5 traces that part with the
    amount (FOCUS bends them away from the source, SPREAD sets how many and how far).
  - **TAPE FRAME:** the tape's own energy as a finite band whose length is FRAME. AGE
    darkens what comes late, STABILITY moves the trace, and ghost passes in the cool hue
    come in with the amount.
  - **TOYBOX:** the recording as quantised blocks (DIGITAL coarser) and the head's path
    under it: straight (FWD), pendulum (TURN), broken (CHAOS), with turn ticks and a
    running light.
  - **MOSAIC:** the partials of the frame being played. DETAIL sets the count, MOTION
    moves the frame along the timeline, TEXTURED shows the residual cloud, and the source
    waveform fades out as the amount rises.
  - **MIRAGE:** a cycle from the recording's partials held at the clock, coarse at the
    left (low notes) and finer at the right. The low-pass's silhouette changes with FILTER
    and TONE.
- **States.** "ANALYZING…" or "PLAYS THE RECORDING" when the mode's data is not there.
- **Card.** The card shows only the amount. Tooltip: "Transform this source using the
  selected Reimagined engine. Click the label to choose a mode."
- **Light.** The REIMAGINED light is the spectral dot.
- **Screenshots.** `design/current/30-reimagined-<mode>.png`, from the canonical scene.

## Tests

- **Core, `[reimagined-modes]`:**
  - The analysis on a vowel, and odd input (noise, silence, 10 ms) reported, never fatal.
  - Every mode at 0 % identical to the plain recording.
  - Every mode at 75 % finite, bounded, at a sane level, and audibly different from every
    other (vowel and pluck).
  - Deterministic, and block-size independent (512 vs 37).
  - Notes end with the envelope (six notes, all released, nothing hangs).
  - Switching mode leaves sounding notes on theirs.
  - Per-layer independence (A TAPE FRAME + B MOSAIC = A + B).
  - Pitch identical at 44.1 / 48 / 88.2 / 96 kHz (within 3 cents, on A4).
  - Fallback to the recording when MOSAIC cannot rebuild.
  - FOCUS / SPREAD move the sound (SPREAD low narrows it).
- **Plugin, `[reimagined-modes]`:**
  - Stable IDs, version hint 11, and the spec's defaults.
  - Every mode's settings saved and recalled per layer, including modes not playing.
  - A state-8 session opens as KALEIDOSCOPE whatever the instance had.
  - Mode automation while notes play: no crash, finite output, no conversion.
  - A gesture on a mode setting converts.
  - The popover opens and closes per layer and not for a missing layer; switching keeps
    the values.
- **Regression.** The 18 reference scenes, and the full core and plugin suites.
- **Hidden.** `[reimagined-audition]` renders every mode at 25/50/75/100 % on real files
  (a chord, low and high notes, soft and hard repeats). `[reimagined-cpu]` measures the
  table above.
- **Corpus.** The analysis ran on all 82 uploaded recordings: 79 rebuilt, 3 reported "no
  stable pitch", 0 failures.

## Known limitations

- **No listening review yet (RI-08, spec §105).** I cannot listen. Tuning was done from
  the design rules, level checks, spectrograms of the audition renders (violin,
  nyckelharpa, voice, organ, analog synth, pluck, kalimba, sailboat, Reese bass,
  flageolet) and targeted click and dip checks. The renders are ready for an ear:
  `OSP_AUDITION_DIR=<out> OSP_AUDITION_SAMPLES="a.wav;b.wav" ./osp_tests "[reimagined-audition]"`.
  The amount curves and defaults are starting points to be tuned by ear.
- **No mode factory presets yet** (spec §78: content after the DSP).
- **Mode switching is per note, not a crossfade.** The spec allows this ("existing voices
  continue old engine"). One effect: a note started at 0 % stays the plain recording if
  the amount is raised while it is held. The KALEIDOSCOPE bus stage fades rather than
  crossfades.
- **LOOP and REVERSE (revised).** Every mode follows the layer's LOOP: on, a held note
  sustains (TAPE FRAME rewinds, TOYBOX and MIRAGE stay in the body, MOSAIC holds its
  spectrum); off, it plays the recording once and ends. Earlier, TAPE FRAME ran out by
  amount whatever LOOP said, and the other modes sustained even with LOOP off. Reversed,
  TAPE FRAME starts at the end (it used to start at position 0 and end at once), TOYBOX's
  walk drifts backwards (it used to drift forwards), and MOSAIC held ping-pongs the body.
  TOYBOX and MIRAGE still reverse without the continuation's mirrored walk. With LOOP off
  TOYBOX's turning head walks through at about half speed, so its note lasts about twice
  the recording.
- **Very high sample rates.** At 192 kHz TOYBOX's far memory tap comes a little closer
  (bounded history).
- **Mirage's filter.** It is the CHARACTER ladder at a separate setting, so the LP24
  character is shared by design.
- **DAW tests** (Logic, Ableton, Reaper: load, save, automation, offline bounce, source
  replacement) still need a Mac (Stage 20). CPU on Apple Silicon is to be measured there;
  earlier ratios suggest about 3–5 times lower than this container.
