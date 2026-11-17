# NOTE ATLAS refinements in ANDOR OSP

_17 Nov 2026. Branch `claude/osp-atlas-refine`._

These are four focused refinements. Nothing else in the interface or the sound changes.

## Audit findings

### D. Round robin with LOOP off: not reproduced

- **One selection path.** `InstrumentEngine::memberFor` chooses the take with LOOP on or off.
- **LOOP off only removes a filter.** With LOOP on, a Stage B set (`InstrumentSet::loopSelects`)
  leaves out members and groups that cannot sustain (`plays`, `groupFor`). With LOOP off,
  every take is a candidate.
- **The order is seeded random, not a strict cycle.** Atlas sets have `cycleTakes = false`, so
  three or more takes play in a seeded random order that never repeats the previous take; they
  do not play 1, 2, 3, 4 in sequence. The specification asks to keep the existing order, so it
  is kept.
- **Verified through the real path.** A dropped bass phrase of eight C2 takes, through
  `analyseRecording` and Stage A / B, plays every take with LOOP off (the take read from the
  voices' members).
- **Likely sources of "it doesn't cycle":**
  - takes whose pitch rounds to different keys or velocity layers form separate groups;
  - with LOOP on, a key whose own note cannot loop is filled from a neighbouring group that may
    hold more takes.

### B. NOTES had no live feedback

- **What the card does.** It inferred the playing region from each read head's position:
  the first region containing it wins.
- **What NOTES did.** It read nothing from the snapshot.

## Changes

### D: tests only

- New research fixtures: `repeated-bass`, `pizzicato`, `fast-bass`, `mixed-duration`.
- `osp_tests [round-robin]` and a plugin test cover:
  - all takes with LOOP off;
  - never the same take twice in a row;
  - deterministic order;
  - unloopable takes still play with LOOP off;
  - a filled key keeps its group's takes;
  - a take never changes while its voice sounds.

### C: FADE IN / FADE OUT

**Where the values live**
- `atlas::Mapping::fadeInMs` (0..100) and `fadeOutMs` (0..500), saved in the atlas JSON only
  when they are not 0 (`docs/analysis-schema.md`).
- They are stored, published and undone exactly as SPEED is: the NOTES edit path, one undo
  step per change ("Fade out 5 ms"), session and portable instruments included.

**DSP** (`InstrumentVoice::edgeGain`)
- The curve is a smoothstep (a raised cosine's shape, without trigonometry).
- **Length in output time.** Transposition and SPEED keep the fade's length; the fade-out reads
  the rate the note plays at now.
- **Boundaries.** It runs from where the note really starts (START, REVERSE) to where it ends.
  The fade-out reaches 0 at the recording's end, not after it.
- **Short notes.** When the fades together exceed 80 % of a note's playback, both are shortened
  in proportion. The saved values are not changed.
- **What it touches.** The recording's contribution only: never SYNTH or the effects. The same
  gain is applied to both channels.
- **When it applies.**
  - Applied with LOOP off only. With LOOP on the values are kept but bypassed: the loop's
    crossfades own a looping note's edges.
  - Bypassed in Granular and in REIMAGINED's engines.
- **PEDAL LOOP.** If PEDAL LOOP takes the note over mid-fade, the gain comes back over 5 ms; a
  looping note is never left faded.
- **Note-off.** AMP release is unchanged; the two gains multiply.
- **0 / 0** bypasses the code: the voice is exactly as before.

**Inspector**
- FADE IN and FADE OUT rows sit under SPEED: minus, the value, plus, Reset. They step like SPEED,
  fine near zero (0.5, 1, 2, 3, 5, 7, 10 ... ms).
- The rows are always shown. With LOOP on they are dimmed, their values still readable, with
  the tooltip "Available when LOOP is off."

### B: live NOTES highlighting

**What the engine publishes**
- `InstrumentVoiceStart::member`: the member `memberFor` chose.
- `GrainSnapshot::playheadMember`: published beside each read head, in the existing lock-free
  snapshot (one relaxed int per head).

**When a read head is reported**
- A voice whose recording has ended (SYNTH holding the note on) no longer reports a read head.
  This applies to the card's read heads too.

**What NOTES shows** (`NoteMapEditor::refreshPlayback`, on its existing 30 Hz refresh)
- The regions of the members actually sounding get a little more of their own hue and brighter
  edges, with a thin read head per voice.
- This covers round-robin takes, FILL's source region and chords.
- Only changed regions are repainted.
- The editing selection and the inspector never move. A replaced sound clears the highlight.

### A: LIBRARY key

- `KeyboardUtilityActions::Action::library` is appended to the shared keys.
- OSP's row is LIBRARY, SETTINGS, INFO: three 31-unit keys 6 apart, exactly the existing
  105-unit area. Nothing else moves.
- LIBRARY opens the existing Library (as the menu's Library... and Cmd+L do) and closes it. It is
  active while the Library is open.
- The tooltip is "Library" / "Close Library". The Learn target is `keyboard.library`, with help.
- There was no library icon in the shared icon set. The key draws book spines in the same line
  weight as SETTINGS' sliders.
- The shell products are unchanged (they never show the key).

## Tests

| Suite | What |
|---|---|
| `osp_tests [round-robin]` | 8 C2 takes with LOOP off and on; pizzicato; FILL; take kept mid-voice |
| `osp_tests [fades]` | 0 / 0 identical; LOOP on and Granular bypass; fade-out timing at the key, +1 st and SPEED 200 %; fade-in with START and REVERSE; stereo; short-note clamping |
| `osp_plugin_tests "note atlas:*"` | real-path round robin; inspector rows, steps, undo, dimmed with LOOP on, session recall, JSON compatibility; NOTES lighting (round robin, FILL, chord, release, replacement, selection kept) |
| `osp_plugin_tests [utility-keys]` | three keys' geometry; LIBRARY opens and closes the Library |

**Fade-out semantics in the tests:** a 20 ms FADE OUT on a 250 ms note is about half-gain 10 ms
before the end and almost silent in its last 3 ms. The same holds a semitone up and at
SPEED 200 % (output time).

## Renders and measurements

`osp_tests "[note-atlas-renders]"` with `OSP_NOTE_ATLAS_RENDERS=<dir>` writes:
- `test1-fast-bass-{A-0-0, B-1-5, C-2-15, D-5-30}.wav`: the fast bass line with the four fade
  pairs;
- `test2-pizzicato-rr-life-{off, normal}.wav`: one key 16 times with LOOP off;
- `test4-loop-{on-fades-bypassed, off-fades}.wav`;
- `cpu.txt`.

These are synthetic fixtures. Nobody has listened on monitors, so no musical judgement is
claimed.

**CPU** (ms of render per second of audio, best of 3, this container):

| Voices | Fades 0 / 0 | Fades 2 / 15 | LOOP on (fades bypassed) |
|---|---|---|---|
| 8 | 72.3 | 82.9 | 89.1 |
| 24 | 112.9 | 98.7 | 100.1 |

The run-to-run noise on this shared machine is larger than the differences (the 24-voice row is
faster with fades on). No overhead is measurable here; a quiet machine is needed for a real
figure. The fade adds one branch per sample when off, and a few multiplies while a fade is
running.

## Screenshots (`note-atlas-refinements/`)

- `01-footer-library-settings-info.jpg`: the three keys (3x).
- `02-full-1448.jpg`: the whole instrument.
- `03-notes-fades-loop-off.jpg`, `04-notes-fades-loop-on-dimmed.jpg`: the inspector.
- `05-notes-chord-sounding-selection-kept.jpg`: A3, E4 and A4 sounding (read heads); C5 selected,
  its inspector kept.
- `06-notes-after-release.jpg`: nothing left lit.

## Limitations, not verified

- **The fade fields step, they do not drag.** They match SPEED's existing stepped cells; there is
  no drag or typed entry, because SPEED has neither.
- **The card still infers its lit regions from positions.** NOTES uses the voices' members.
- **No real DAW**, no Retina screen and no listening on real bass or pizzicato phrases.
- **The round-robin report was not reproduced.** A real phrase showing it would locate the cause
  (likely pitch grouping).
