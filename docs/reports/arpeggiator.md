# ARPEGGIATOR: one arpeggiator ahead of the voice engine

A note-event stage between the incoming MIDI (the host's and the on-screen keyboard's) and the
instrument's voice allocation. It is turned on by a light next to the keyboard and edited in an
inline panel that opens between the macros and the keyboard. Parameters v15. The state version
is unchanged: parameters and one view property were added.

## 1. Audit (ARP-0)

What the code was before the change. The code was taken as authoritative, not earlier notes.

**MIDI path.** `processBlock` runs in this order:
1. Merges the screen keyboard (`keyboardState.processNextMidiBuffer`).
2. Swaps a pending instrument.
3. Reads the play head into one `HostTiming` per block. A transport start resets performance memory.
4. Applies the screen wheels and the parameters.
5. Renders sample-accurately: `renderTo (event)` then `handleMidi (event)`.

`handleMidi` covers note on/off, pressure, CC 74, CC 1, CC 20–27, MPE member bends, CC 64 and
All Notes/Sound Off.

**Voice engine.** `InstrumentEngine` knows a note by its pitch (and its channel with MPE), with
no per-note handle. `releaseNote` releases every keyed voice of the pitch. While the pedal is
down it marks them pedal-held instead.

Consequence: with a gate above 100 %, the previous note's note-off would end the new note of
the same pitch. The arpeggiator therefore ends a still-sounding pitch right before it plays it
again (§3).

**Editor.** Everything is laid out once on the 1448 × 1086 reference canvas and scaled. The
window has a fixed aspect ratio (869–1810 px wide). Advanced was a large button at the bottom
right.

**State.** APVTS parameters, plus view properties on the state tree (`uiScale`,
`advancedOpen`). A parameter missing from a session is reset to its default.

**Baseline.** A hidden null test `[.][arp-baseline]` was written and committed *before* any
arpeggiator code (commit `640800e`). The old build saved 13 sessions and rendered them:
- one-shot;
- host transport with SHAPER;
- reverse;
- granular;
- mono + glide;
- two layers;
- three layers;
- the FX chain (LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO);
- each REIMAGINED mode.

The test MIDI holds a chord and uses mod wheel, pitch bend and pressure. The pedal holds a
released note. It plays a repeated pitch, a note-off and a note-on on the same sample, and All
Notes Off.

## 2. Architecture

```
host MIDI + screen keyboard ─► keyboardState merge ─► Arpeggiator (src/engine, pure C++)
                                                        │ note on/off, CC 64, All Notes Off in
                                                        │ its notes (and everything else) out
                                                        ▼
                                  existing render loop: renderTo (offset) / handleMidi (event)
                                                        ▼
                               InstrumentEngine (voices, layers, REIMAGINED, FX: unchanged)
```

- **One arpeggiator** for the instrument, with no per-layer copies and no second engine. Its output
  is plain note events, so every layer, mode and effect plays as if the notes had been played by
  hand. No DSP code changed.
- **Off means untouched.** While it is off, the plugin only *follows* the held notes, so it can
  hand them over when switched on. The `MidiBuffer` the engine plays is the one that arrived,
  untouched (§6).
- **On.** Note events and CC 64 go to the arpeggiator. Everything else passes through at its
  sample: bends, wheels, pressure, CC 74, CC 20–27 and All Notes Off. The arpeggiator's events
  are merged into a preallocated `MidiBuffer` that `prepareToPlay` sizes. The merged buffer then
  goes through the existing render loop, so offsets stay sample-accurate.
- **Real-time safety.** The arpeggiator itself has fixed capacity: 64 held notes, 2048 events per
  block, a per-pitch note-off table. No allocation, no locks.
- **Telemetry.** After each block, the processor publishes 16 low/high pitches, the column
  sounding and an active flag as atomics. The editor reads them at 30 Hz (`arpView()`).
- **Headless.** `research::arpeggiate` runs the same class over a `MidiSequence`, block by block,
  with a virtual transport. Any render config can carry an `"arp"` block, for engines A, B and C
  (`research/configs/arp-c-updown-16th.json`).

## 3. Behaviour

| | |
|---|---|
| Patterns | UP; DOWN; UP/DOWN (no repeated ends: C E G E C E…); PLAYED (press order); RANDOM; CHORD (all held notes each step) |
| Octaves | 1–4, octave by octave (C E G, C' E' G', …). Copies above MIDI 127 are left out (never wrapped or clamped onto 127). CHORD climbs one octave per step over the octaves its lowest note can reach |
| Rates | 1/4, 1/8, 1/16, 1/32; 1/4D, 1/8D, 1/16D; 1/4T, 1/8T, 1/16T (choice order is saved: never reorder) |
| Gate | 10–150 % of a step. Above 100 % different pitches overlap (legato) |
| Swing | 0–100 % (`arp.swing`, version hint 16, default 0 = straight): every second step plays later by up to half a step (100 % = a dotted feel). On the host's grid the odd grid positions swing, so the swing sits on the beat wherever the chord began; free running, the odd steps of the phrase. Gate lengths stay those of a straight step |
| Velocity | each step plays its note's own velocity; a note pressed again takes its new velocity |
| RANDOM | `osp::Prng`, seeded from the variation seed and a phrase counter that restarts when the host's transport starts (bounces repeat). Never the same note twice in a row (with 2+ notes). Counter-based, so the display shows the real upcoming choices |
| Host playing | steps on the host's PPQ grid (multiples of the step), placed to the sample. A note pressed between steps waits for the next one; a note on the grid sample plays on it |
| Loops, jumps | anything but the continuation of the previous block (more than 64 samples or 1 % of the block off) resynchronises: the next boundary plays, missed steps are not caught up. A small late step (tempo drift) plays once, at once |
| Tempo changes | follow the grid (PPQ based); a rate change continues after the last boundary of the old grid |
| Host stopped / no host | free running at the last tempo the host reported (120 BPM without one); the first step of a phrase sounds at once |
| Note-offs | every generated note has its own scheduled note-off (absolute sample time). At equal samples note-offs come first. A pitch still sounding is ended right before it plays again |
| Sustain (CC 64) | while ARP is on, the pedal keeps released keys in the pattern; generated notes still end at their gate; the engine never sees the pedal |
| All Notes Off / Sound Off | clears held notes, the pedal and the scheduler; sounding notes end at once; the message also reaches the engine |
| Switching on | the keys the engine was playing directly end (and the pedal is released at the engine); the pattern starts from what is held, pedal-held keys included |
| Switching off | generated notes end; keys still held sound again directly at their velocity; the pedal goes back to the engine. Keys only the pedal held are not restarted |
| Pitch bend, mod wheel, pressure, CC 74, CC 20–27 | pass through untouched |
| Screen keyboard | the same path (it is merged before the arpeggiator) |
| MPE | generated notes keep their key's channel; MPE has not been tested with the arpeggiator |

## 4. GUI

Laid out after the approved mockups (supplied after the first version, which had been built
from the text alone):

- **Beside the keyboard**, right of its last key and level with it, two cards. The keyboard
  is 1150 reference px wide instead of 1262 to make room, and still shows all 88 keys, C1–C8.
  - **The ARP card:**
    - a power light: an orange ring when on, a grey lens when off. It switches
      `arp.enabled` and nothing else;
    - **ARP**;
    - a chevron: down when the editor is hidden, up when it is shown;
    - below them, in a small inset, the pattern and rate ("UP · 1/8").
    - A click anywhere on the card except the light shows or hides the editor, and nothing else.
  - **Advanced ›**: a raised card under it, pressed in while Advanced is open, with the same
    popover as before.
- **Inline editor** (`ArpInlinePanel`): a raised panel like the macro panel, between the
  macros and the keyboard row, 177 px high. It contains:
  - **ARPEGGIATOR**, with no power toggle (by your choice), and a round collapse button
    at the top right;
  - **16 numbered steps.** Each step is a bar as high as its note (a chord: its top note).
    The step sounding is orange and marked with a dot under the strip. Steps played earlier on
    the page are a lighter orange, and steps to come are tan. All of it comes from the audio
    thread's scheduler. While nothing is held, the strip shows the pattern's shape on a C
    major chord, faint;
  - **PATTERN** (revised; after Live's Arpeggiator). It has two parts:
    - A small picture of the style: the notes of a four-note chord (C E G B) as dots joined
      over twelve steps, drawn by the same scheduler. CHORD shows columns. The style under the
      pointer in the list previews there, in tan.
    - A list of all 18 styles beside it: six rows in view, a slim scroll track, the wheel
      scrolls. The chosen style is tinted and stays in view when automation or a preset
      changes it. The list is grouped: the straight walks, then converge / diverge, the finger
      patterns, Played, Chord, and the random styles.
  - **RATE** (revised) is a knob that walks the divisions from slowest to fastest: 1/4D, 1/4,
    1/8D, 1/4T, 1/8, 1/16D, 1/8T, 1/16, 1/16T, 1/32. The parameter keeps its saved choice
    order, and only the knob maps its position onto it (`MiniKnob::setChoiceOrder`).
  - **OCTAVES** (revised) is four stacked keys, 1 on top to 4 at the bottom; the one chosen
    is lit.
  - **GATE** and **SWING** are the instrument's small knobs (the envelope's), with orange arcs
    and their values.
  - Everything is dimmed while ARP is off.
- **Layout.** When the editor shows, the keyboard row and footer move down by 187 reference px.
  The instrument (housing included) grows by the same, the window grows by that height times its
  scale, and its width is unchanged. When hidden, the window returns to its previous size. The
  resize limits and aspect ratio follow. **Resize fallback:** if a host keeps the old window
  size, the whole instrument is scaled to fit with a margin and nothing is cut off.
- **Animation:** none. The window takes its new height in one step, because host-driven resizes
  animated frame by frame lag or flicker in several hosts and I could not test any host.
- **`arpEditorExpanded`** is a view property (`arpExpanded` on the state tree, like
  `advancedOpen`). It is not a parameter and is never read by audio.

## 5. Parameters and compatibility

| ID | Type | Default |
|---|---|---|
| `arp.enabled` | bool | off |
| `arp.pattern` | choice UP, DOWN, UP/DOWN, PLAYED, RANDOM, CHORD, then (appended) DOWN/UP, UP & DOWN, DOWN & UP, CONVERGE, DIVERGE, CON & DIVERGE, PINKY UP, PINKY UP/DOWN, THUMB UP, THUMB UP/DOWN, RANDOM OTHER, RANDOM ONCE | UP |
| `arp.rate` | choice 1/4 … 1/16T (above) | 1/8 |
| `arp.gate` | 10–150 % | 75 % |
| `arp.octaves` | 1–4 | 1 |
| `arp.swing` | 0–100 % (version hint 16) | 0 % |

All are automatable: SWING has version hint 16, the others 15. Old sessions contain no `arp.*`, so the
arpeggiator opens off with defaults, even in an instance where it was on (tested). They also
contain no `arpExpanded`, so the editor opens hidden.

## 6. Results

- **Release blocker: old sessions with ARP off.**
  - The 13 sessions the build before the arpeggiator saved were loaded by the new build and
    played the same MIDI (with and without a host transport).
  - All 13 are bit-identical: largest difference 0 in every scene.
  - Writing and comparing in the old build was also bit-identical, so the test is deterministic.
  - Re-run after the GUI changes: still 0.
- **Timing (unit).** Host grid at 60, 90, 120 and 174 BPM × 44.1, 48 and 96 kHz × 32, 64, 128,
  256 and 512 blocks × all ten rates: 600 cases.
  - Every step lands on exactly the expected sample.
  - No double steps, no missed ones.
  - Free running: 44.1, 48 and 96 kHz × blocks 1 to 4096 × four gates; every step and every
    note-off lands on its expected sample.
- **Patterns:** 0, 1, 2, 3, 4, 7, 10, 16, 64 and 70 notes with every pattern and octave count.
  Beyond 64 notes, the first 64 pressed play.
- **No stuck notes.** A stress test of 60 random performances:
  - inputs: notes, pedal and panics; random on/off, pattern, rate, gate and octave changes;
    transport jumps and stops; blocks of 1 to 2048 samples at three sample rates;
  - checked against a model of the engine's release rules;
  - result: nothing left sounding, and no pitch retriggered while still sounding.
- **Plugin** (`[plugin][arp]`, 7 cases, 925 assertions):
  - parameters, recall, old sessions and the view setting (expanding does not change the audio);
  - steps with their velocities; the screen keyboard; pitch bend passing through; the pedal;
  - the host grid; a RANDOM bounce that is the same in two instances, one of which played before;
  - every REIMAGINED mode with both routings (per-layer and legacy global), plus LIFE, the three
    DRIVE circuits, DYNAMICS, CHARACTER, all five MOVEMENT modes, SPACE and ECHO, each with UP,
    RANDOM and CHORD: all finite, bounded and audible, and every voice ends;
  - REVERSE × LOOP × Granular × 1–3 layers;
  - lifecycle: switching every 37 blocks while a chord and the pedal are held; All Notes Off; a
    sample-rate change and a session recall mid-performance; Mono + glide; blocks of 1 and 4096
    and an empty block.
- **Headless:** the chords fixture through baselines A and B and engine C, with UP/DOWN, 1/16,
  two octaves at 110 BPM, renders cleanly (status ok).
- **CPU.**
  - The arpeggiator stage itself (no sound loaded, 1/32 CHORD over four octaves with 16 notes
    held, 128-sample blocks) costs 0.08–0.38 % of real time on this VM.
  - With sound, the cost is the engine playing the notes the pattern makes. On the VM:
    - 16 notes, ARP 1/16 UP: 6.5 % (16 held notes without ARP: 13.5 %);
    - 4 notes, 1/16 CHORD, two octaves: 21 % (24 voices, mostly release tails at the default
      700 ms release);
    - three layers, 1/16 RANDOM, three octaves: 38.5 %;
    - a stress case, 16-note chords at 1/32 up to three octaves above the keys: 155 %. That is
      the engine's cost for many voices transposed far up; "16 notes, +24 st" already measures
      78 % without the arpeggiator.

## 6b. The Live styles (added later)

Twelve styles from Ableton Live's Arpeggiator were added. They are appended to `arp.pattern`, so
saved sessions keep their pattern: the value is stored by index and the first six did not move.
Each style is a walk over the held notes sorted by pitch and spread over OCTAVES, the same list
UP walks. Below, C E G B are the notes held.

| Style | Walk | Notes |
|---|---|---|
| DOWN/UP | G E C E | the mirror of UP/DOWN; the ends once |
| UP & DOWN | C E G B B G E C | the ends twice |
| DOWN & UP | B G E C C E G B | |
| CONVERGE | C B E G | from the outside in |
| DIVERGE | G E B C | from the inside out |
| CON & DIVERGE | C B E G E B | in, then out again, the ends once |
| PINKY UP | C B E B G B | the top after each note going up |
| PINKY UP/DOWN | C B E B G B E B | the same, going up and down |
| THUMB UP | C E C G C B | the bottom before each note going up |
| THUMB UP/DOWN | C E C G C B C G | the same, going up and down |
| RANDOM OTHER | every note once, in a random order, then a new order | never the same note twice in a row, not even where one order meets the next |
| RANDOM ONCE | one random order, repeated | a new order with each new phrase |

- **Determinism.** The random orders are Fisher–Yates shuffles from `osp::Prng`, seeded from the
  phrase seed (the stored variation seed and the phrase counter). A bounce is therefore the same
  every time, as for RANDOM.
- **Few notes.** With one note every style repeats it. With two, each style alternates them or
  walks them as named; RANDOM OTHER takes them in turns from a random start.
- **Not added.** Live's Play Order and Chord Trigger are the existing PLAYED and CHORD. Live's
  options around the styles (Hold, Offset, Repeats, Retrigger, Transpose, Velocity decay) are
  not part of this change.
- **Automation lanes.** A host stores a choice parameter's automation as 0–1, and that span now
  covers 18 choices instead of 6. Automation of PATTERN recorded with an older build would
  therefore pick other styles. Sessions and presets are not affected, because they store the
  index.

## 7. Tests

- **Unit** (`[unit][arp]`, 19 cases): rates and names; all patterns and octaves (the Live
  styles walk C E G B as in §6b, over two octaves, with one and two notes; RANDOM OTHER plays
  each note once per round with no repeat at the seam, and RANDOM ONCE repeats one order, both
  reproducible from the seed); 0–70 notes;
  velocities; free-running timing and gate; the host-grid matrix; entering the grid; loops,
  jumps, tempo changes, transport stop and start, no host; gate above 100 %; pedal; panic;
  hand-overs; the stress test; RANDOM determinism and restart; the display; determinism of a
  whole performance; the research config and `arpeggiate`.
- **Plugin** (`[plugin][arp]`, 7 cases): §6. Hidden ones:
  - `[arp-baseline]`: the null test, `OSP_ARP_BASELINE=write|compare`, `OSP_ARP_BASELINE_DIR`;
  - `[arp-ui]`: the GUI and screenshots, with xvfb;
  - `[cpu-profile]`: now with four ARP cases.
- **GUI** (`[arp-ui]`):
  - the light switches only enabled, and the chevron only the editor;
  - the window grows by the panel height × scale with the same width, and shrinks back exactly;
  - the keyboard control moves down by 127 px, and the panel ends above it;
  - the lit column matches the scheduler's current step;
  - the editor reopens as it was saved;
  - ADVANCED still opens Advanced.

## 8. Not tested, and limitations

- **No DAW was used.** Nothing here has run in Logic, Ableton, Reaper or any other host:
  - window resizing in hosts (AU/VST3 resize requests, Logic's AU resize behaviour);
  - automation of `arp.*` from a host lane;
  - offline bounce in a host (tested only as a simulated transport);
  - real loop regions;
  - tempo-ramp automation;
  - sample-rate and buffer changes initiated by a host.

  All of these are covered by headless tests with a simulated transport only.
- **MPE** with the arpeggiator is not tested. Generated notes keep their key's channel.
- **Not listened to.** I can't listen; the musical feel (gate default, UP/DOWN ends, CHORD
  octave climb) is to the spec, and is yours to judge.
- **Same pitch, gate above 100 %:** the engine knows notes only by pitch, so the earlier note is
  ended at the instant the same pitch plays again. It is not held under the new one.
- **Switching off with the pedal down:** keys that only the pedal held are not restarted directly.
- **More than 64 held notes:** the extra notes are ignored while ARP is on.
- **No mockups:** layout, sizes and the exact amber are my choices from the text; see §4.
- **Mouse interaction** (hover, the pattern list, octave keys, RATE and GATE drags) was
  exercised through the code paths the tests call, not with real mouse events.
