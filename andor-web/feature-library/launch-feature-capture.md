# Launch feature capture: production setups for nine stories (Phase 6)

**What this is:** each story is built around one audible musical payoff. Every capture is a Virtual Studio project (`andor-web/virtual-studio`):
- your MIDI, automation, bounce, sources and preset go in;
- the plugin's own frames come out, at 24 fps (cinematic) or 30 fps (delivery), on one sample clock.

**Three kinds of footage, kept apart in names and folders (Launch Kit v2):**

| Kind | What it is |
|---|---|
| `capture` | The plugin's own drawing, rendered headless from your performance (silent frames, plus your bounce as audio) |
| `screen` | A screen recording of the plugin on your Mac, with sound |
| `cg` | The Blender instrument driven by the same performance |

**Synthetic material** (test signals, the technical test performance) is for development only, and is labelled `workflow-test` wherever it appears.

**Your material per story:** a dry source recording (or several), the performance you play, its bounce, and the preset. Capture runs after you send them. Nothing here needs finished music to set up.

---

## 1. One Sound: the flagship film (60–90 s)

| | |
|---|---|
| **Payoff** | A single dry note, then the same note as a playable instrument |
| **You record** | (a) the dry source, 3–8 s; (b) a 30–60 s performance on that one sound, from intimate to full |
| **OSP state** | One layer, the source alone. Macros moved as you play (LIFE, FILTER, MOVEMENT, SPACE). REIMAGINED enters halfway |
| **Capture** | Project `001-one-sound-<instrument>`: performance plus automation at 24 fps |
| **Shots** | `screen`: the drop and the first note. `capture`: the full performance (displays, read heads). `cg` D05 performance variant (keys from your MIDI). `cg` D03 (macros turning to your values). D01 W reveal to close |
| **Audio** | Your bounce; dry vs instrument level-matched |

## 2. Phrase Transformer: an expressive phrase becomes a playable instrument (30–45 s)

| | |
|---|---|
| **Payoff** | Your phrase, then the same phrase's notes played as a new melody |
| **You record** | A monophonic phrase with a repeated note and a gap (e.g. nyckelharpa, voice) |
| **OSP state** | The phrase dropped on A, **Map notes → yes**. NOTES open for the atlas shots |
| **Capture** | `capture` of the NOTE MAP while you play: round-robin takes light up, a filled gap lights its neighbour (the Phase 5 workflow test proved each step) |
| **Shots** | `screen` the drop and map. `capture` NOTES with live regions. `cg` keybed tracking (the lateral key-tracking study) under your new melody |

## 3. Live Capture: retrospective sampling performance (20–30 s)

| | |
|---|---|
| **Payoff** | You play an instrument into OSP, press GRAB LAST, and play what you just played |
| **You record** | A screen recording with sound (the host input routing is real), plus a parallel bounce of the input |
| **OSP state** | Empty instrument → **Record…** (CAPTURE page) → play → **GRAB LAST** (10 / 20 / 30 / 60 s) |
| **Capture** | `screen` is primary: this story needs a real input |
| **Note** | Headless capture can reproduce the page from the recorded input (Phase 5 F2), but the flagship take is a real screen recording |

## 4. Three independent arpeggiators (20–30 s)

| | |
|---|---|
| **Payoff** | A pad, a pulse and runs, from one held chord |
| **You record** | Three sources (pad, percussive, bright pluck), and a performance holding chords |
| **OSP state** | LINK off. A plays nothing (the pad). B: CHORD at 1/8. C: UP & DOWN at 1/16 × 3 (or your choice). Custom patterns only if verified in the release build |
| **Capture** | `capture` at 24 fps; the ARP panel open for one shot |
| **Physical vs internal notes** | In `cg`, the keys move only for the chords you hold. The arpeggio shows in the interface (displays and keyboard indicators), never on the physical keys. The technical test performance verifies this (5–7 s) |

## 5. KEY DNA (20 s)

| | |
|---|---|
| **Payoff** | One sound whose low, middle and high registers each have their own grain character |
| **You record** | A sustained source and a three-register performance |
| **OSP state** | Granular. KEY DNA on: SIZE long → short, DENS sparse → dense, POS variation, PAN ramp (the Phase 5 settings, or yours) |
| **Shots** | `capture`: the KEY DNA editor (curves) once; then the granular display per register |
| **Modal views** | The KEY DNA editor covers the card in the real editor. It is shown as a `capture` cut, never projected onto the Blender instrument's display |

## 6. SYNTH + IMPRINT (20–25 s, level-matched)

| | |
|---|---|
| **Payoff** | The source, a pure oscillator, a blend, and the oscillator imprinted with the source's spectrum: the same phrase each time |
| **You record** | One rich tonal source and one short phrase |
| **OSP state** | SYNTHESIZE 0 → 100 Analog → 50 Analog → 100 Imprint (imprint about 80) |
| **Shots** | `capture` of the card (the SYNTH chip and its value), with your four level-matched bounces. The difference is audible, not visible, so audio carries this story |

## 7. REIMAGINED: one sound, five worlds (30–40 s)

| | |
|---|---|
| **Payoff** | One sound through Kaleidoscope, Tape Frame, Toybox, Mosaic and Mirage |
| **You record** | One source and one 6–8 s phrase, played five times (or once, with the mode automated at the bar lines) |
| **OSP state** | REIMAGINED at about 60–80 %. The mode changed per pass, from the modes popover |
| **Capture** | `capture` of the REIMAGINED arc and display per mode; the modes popover once. `cg` D03 with the REIMAGINED knob turning to your values |

## 8. Soundless templates (15–25 s)

| | |
|---|---|
| **Payoff** | A starting state with no sound, ready for yours: drop a sound and it already plays as designed |
| **You record** | Two contrasting sources dropped into the same template |
| **OSP state** | Library → STARTING STATES / TEMPLATES → a template, then the drop |
| **Capture** | `screen` (the library browser and the drop are interface-led). `capture` of the two resulting instruments under the same performance |

## 9. MOVEMENT and SHAPER (20–30 s)

| | |
|---|---|
| **Payoff** | A static sound breathing in time; a drawn SHAPER pattern locked to the groove |
| **You record** | A held chord, then a groove, with MOVEMENT automated and the host tempo set |
| **OSP state** | MOVEMENT rack: SHAPER CUSTOM pattern drawn (saved with the session) |
| **Capture** | `capture` of the MOVEMENT rack (the playhead follows the host tempo in the capture's timeline). `cg` D03 with MOVEMENT turning to your values |

---

## Per-story checklist (for every take)

- [ ] Dry source(s), 24-bit WAV, with usage rights noted.
- [ ] Performance: MIDI clip, automation lanes (or CC 20–27 for macros), bounce from the same start.
- [ ] Preset / plugin state saved, and the INFO build noted.
- [ ] `vs.py ingest` counts checked (notes, bend, CC, lanes).
- [ ] Capture at 24 fps (film) and/or 30 fps (social).
- [ ] Footage kind recorded in the kit manifest (`capture`, `screen` or `cg`).
