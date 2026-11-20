# ANDOR Learn: coverage matrix and proposed public curriculum (Gate 0)

_Commit audited: `6ef3376` (release line). Every label below was read from the code, not from older
docs; file references are in the Gate 0 fact sheets summarised in `gate-0-report.md`._

Legend
- **Lesson** — in-plugin Learn 2.0 lesson (`OspLearn.cpp`) and its step IDs.
- **Info** — in-plugin INFO topic (target ID).
- **Targets** — Learn target IDs a scenario can use as-is; _new_ = a test-side locator is needed
  (found by parameter ID or component, in the runner only — no plugin change).
- **Class** — interaction class (button, knob, popover, PopupMenu, drop, keys, input, async).
- **Capture** — ✅ capturable now through the real UI; ⚠️ capturable with a disclosed limitation;
  ⛔ blocked.

## Coverage summary (the brief's checklist)

| Feature | In-plugin lesson | Info topic | Public guide needed |
|---|---|---|---|
| Phrase Transformer / Note Atlas / MAP NOTES | ✅ `osp.phrase` (map, play, notes) | ✅ `osp.source.notes` (NOTE MAP) | yes |
| Record from a live input | ❌ none (STATUS NEXT) | ❌ none (REC key has a tooltip only) | **yes — new** |
| GRAB LAST | ❌ none | ❌ none | **yes — new** |
| Per-source arpeggiators | ❌ (`osp.arp` teaches layer A's lane only) | partial (`osp.arp.control`) | **yes — new** |
| Custom ARP patterns | ❌ | ❌ | **yes — new** (release line only) |
| STRUM | ❌ | ❌ | **yes — new** |
| SATELLITE | ❌ | ❌ | **yes — new** |
| SUB | ✅ `osp.sub` | ✅ `osp.sub.row` | yes |
| SYNTH / IMPRINT | ❌ (STATUS NEXT) | ❌ | **yes — new** |
| USER macros | ✅ `osp.usermacros` | ✅ `macros.bank` | yes |
| Pitch / mod wheel, VIBRATO | ✅ `osp.vibrato` | ✅ `osp.wheel.pitch`, `osp.wheel.mod`, `osp.vibrato` | yes |
| Modulation routes | ✅ `osp.modulation` | ✅ `osp.mod.panel`, `osp.mod.halo` | yes |
| MIDI Learn (right-click → Learn MIDI CC) | ❌ | ❌ | optional (release line only) |

## Group 1 — Make an instrument

| # | Scene | Lesson / steps | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 1.1 | Load a one-shot from a file | `osp.quickstart` / setup (load) | `osp.source.card` | `osp.source.dropZone` | drop, async | ⚠️ | OS drag cannot be synthesised: real `filesDropped` + drawn file badge (D3). Browse… opens a native dialog (not capturable). |
| 1.2 | Play it from the keyboard | `osp.quickstart` / play | `osp.keyboard` | `osp.keyboard` | keys | ✅ | on-screen keys clicked, or MIDI shown as lit keys |
| 1.3 | Load a phrase of several notes | `osp.phrase` / setup | — | `osp.source.dropZone` | drop, async | ⚠️ | as 1.1; uses the shipped `phrase` example (7 sung notes, keys 57 60 64 69) |
| 1.4 | MAP NOTES | `osp.phrase` / map | — | `osp.source.mapNotes` | button, async | ✅ | the card asks "Identify notes? · N heard" with **MAP NOTES** / **NO**; card ⋮ → "Map notes…" is the alternative |
| 1.5 | Inspect and play mapped notes | `osp.phrase` / play, notes | `osp.source.notes` | `osp.source.notes`, `osp.keyboard` | button, keys | ✅ | NOTES editor ("NOTES A", status Analyzing → Ready) |
| 1.6 | Record from a connected input | — | — | _new_: REC key, CAPTURE page parts | button, input, async | ⚠️ | real input bus + deterministic fixture; host routing step host-pending (B2) |
| 1.7 | GRAB LAST | — | — | _new_: GRAB LAST cells | input, button, async | ⚠️ | input history fed before any click; same host flag |
| 1.8 | Save / export the instrument | `osp.presets` / save, export | `osp.header.preset`, `osp.header.menu` | `osp.library.save`, `osp.header.menu` | button, PopupMenu | ⚠️ | Save… opens a name field (capturable); Export writes a file through a native chooser: film up to the menu item, state the result in text |

## Group 2 — Play and layer

| # | Scene | Lesson / steps | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 2.1 | One Shot ↔ Granular | `osp.playback` / oneshot, granular | `osp.source.mode` | `osp.source.mode` | PopupMenu | ✅ | the playback key's menu reads "One Shot" / "Granular" |
| 2.2 | Add a second and third source | `osp.layers` / add, third | `osp.header.menu` | `osp.header.menu`, `osp.source.dropZone` | PopupMenu, drop | ⚠️ | ••• → "Add source…" opens the native chooser; dropping beside the card shows ADD LAYER (drop limitation as 1.1) |
| 2.3 | Each source's level | `osp.layers` / balance | `osp.source.level` | `osp.source.B.level` | knob | ✅ | |
| 2.4 | Mute and Solo | `osp.layers` / solo, unsolo | `osp.source.mute` | `osp.source.B.solo`, `.mute` | button | ✅ | |
| 2.5 | Load a preset or starting state | `osp.presets` / library | `osp.header.preset`, `keyboard.library` | `osp.header.preset`, `osp.library` | button, panel | ✅ | Library window is an editor child |
| 2.6 | Preset vs exported instrument | `osp.presets` / preset, export (manual) | — | — | text | n/a | reference article, no video needed |

## Group 5 — Granular and KEY DNA

| # | Scene | Lesson / steps | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 5.1 | Enter Granular | `osp.playback` / granular; `osp.keydna` / granular | `osp.source.mode` | `osp.source.mode` | PopupMenu | ✅ | shares 2.1's capture |
| 5.2 | Grain position | `osp.playback` / pos | `osp.granular.position` | `osp.granular.position` | knob (hover reveal) | ✅ | the strip appears after hovering the display (380 ms): real-time pacing needed |
| 5.3 | Grain size / density | `osp.playback` / size, dens | `osp.granular.size`, `.density` | same | knob | ✅ | |
| 5.4 | Open KEY DNA | `osp.keydna` / open | `osp.source.keyDna` | `osp.source.keyDna` | button | ✅ | |
| 5.5 | Edit individual keys | `osp.keydna` / position (drawn keys accepted) | — | `osp.keydna.graph`, `osp.keydna.depth` | drag | ✅ | start state with a drawn, clearly uneven curve (set-up) so key differences are visible |
| 5.6 | Range / variation | `osp.keydna` / other, amount | — | `osp.keydna.tabs`, `osp.keydna.amount` | button, knob | ✅ | |

## Group 6 — Shape and finish

| # | Scene | Lesson / steps | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 6.1 | REIMAGINED modes | `osp.reimagined` / open, mode0–4 | `osp.source.reimagined` | `osp.source.reimagined.label`, `osp.reimagined.mode` | popover | ✅ | KALEIDOSCOPE, TAPE FRAME, TOYBOX, MOSAIC, MIRAGE |
| 6.2 | REIMAGINED amount | `osp.reimagined` / amount | same | `osp.source.reimagined` | knob | ✅ | |
| 6.3 | MOVEMENT mode | `osp.sound` / movement, movement.mode | `osp.macro.movement` | `osp.macro.movement.label`, `osp.movement.mode` | popover | ✅ | DRIFT, TAPE, CHORUS, PULSE, SHAPER |
| 6.4 | SHAPER pattern | — | (in MOVEMENT topic) | _new_ | popover/rack | ✅ | lesson does not cover SHAPER (SHAPER DNA excluded on purpose) |
| 6.5 | Modulation route | `osp.modulation` / tab, assign, depth | `osp.mod.panel`, `osp.mod.halo` | `osp.mod.lfo1`, `osp.mod.halo` | drag (tab → knob) | ✅ | the tab drag is a real drag on the editor; cable follows the pointer |
| 6.6 | Assign a USER macro | `osp.usermacros` | `macros.bank` | `macros.bank`, `osp.source.level`, `usermacro.0` | button, right-click PopupMenu, drag | ✅ | "Assign to Macro" → "Macro 1" |
| 6.7 | FILTER / ECHO / SPACE | `osp.sound` / filter, echo, space | `osp.macro.filter`, `.echo`, `.space` | same | knob, popover | ✅ | |
| 6.8 | Save the result | `osp.presets` / save | `osp.header.preset` | `osp.library.save` | button | ✅ | shares 1.8 |

## Group 3 — SYNTH and IMPRINT

Verified: each card has a chip drawn **SYNTH** with the amount ("NN%"); its tooltip says SYNTHESIZE.
Clicking the name opens the SYNTH page in the card's display: **SYNTH A**, **ANALOG | IMPRINT**,
**AMOUNT**, **WAVE** (four glyphs, no text: saw, square, triangle, sine), **OCTAVE** (−2 … +1),
**DETUNE** (0–50 cents), **TONE**, **SHAPE** **Held | Follow** (default Follow), **PITCH** **Keys |
Recording**; IMPRINT mode adds **IMPRINT** and **MOTION** (default 100 %). No lesson, no Info topic,
no Learn target.

| # | Scene | Lesson | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 3.1 | Find SYNTH on a card | — | — | _new_: the SYNTH chip (`layerA.synthesize` tag) | — | ✅ | the guide calls it SYNTH (as drawn) and mentions SYNTHESIZE once |
| 3.2 | Blend the oscillator in | — | — | _new_ (chip amount drag) | drag | ✅ | |
| 3.3 | Waveform and tuning | — | — | _new_: `SynthEditor::choiceArea` (WAVE, OCTAVE, PITCH) | painted segments | ✅ | WAVE has no text labels: the step names the shapes in text |
| 3.4 | SHAPE: Held / Follow (amplitude following) | — | — | _new_ | painted segment | ✅ | Follow follows the recording's own level contour; Held is full for the note, shaped by AMP |
| 3.5 | IMPRINT and MOTION | — | — | _new_ (`layerA.synth.imprint`, `.motion` fields) | segment, field drag | ✅ | must wait out "Preparing spectral imprint…" (async state) |
| 3.6 | The result in the editor | — | — | the card | — | ✅ | merge into 3.5's end hold |

## Group 4 — Arpeggiator

Verified: the key beside the keyboard has two rows, **ARP** and **SUB**, each with a light (on/off)
and a name (opens). The panel is titled **ARPEGGIATOR**; its title row has **PLAYS** with chips **A B
C**, **LINK**, and — when LINK is off — **EDIT A | B | C**, then the **SATELLITE** and **STRUM** keys.
Body: 16-step display, **PATTERN** list (18 styles: Up, Down, Up/Down, Down/Up, Up & Down, Down & Up,
Converge, Diverge, Con & Diverge, Pinky Up, Pinky Up/Down, Thumb Up, Thumb Up/Down, Played, Chord,
Random, Random Other, Random Once, then **Custom Patterns ›**), **RATE** (1/4D … 1/32), **OCTAVES**
1–4, **GATE**, **SWING**, **VELOCITY**. With LINK off, PATTERN, RATE, OCTAVES, CUSTOM and STRUM are per
card; GATE, SWING and VELOCITY stay shared. An unlit PLAYS chip makes that layer hold the keys as
played while the others arpeggiate. Most of the panel is painted (not separate components), so the
runner locates parts through the panel's public geometry (`cardChipArea`, `linkArea`, `editTabArea`,
`customRowArea`…), which is test-side only.

| # | Scene | Lesson / steps | Info | Targets | Class | Capture | Notes |
|---|---|---|---|---|---|---|---|
| 4.1 | Open and enable ARP | `osp.arp` / open, on | `osp.arp.control` | `osp.arp.control`, `osp.arp.enable` | button | ✅ | |
| 4.2 | Pattern and rate | `osp.arp` / pattern, rate | — | `osp.arp.pattern`, `osp.arp.rate` | painted list (scroll), knob | ✅ | the list is in the panel, not a PopupMenu |
| 4.3 | Independent patterns for A, B, C | — | — | _new_: LINK, EDIT tabs | painted chips, list, knob | ✅ | three loaded layers; LINK off; EDIT B → Up, EDIT C → Chord… |
| 4.4 | ARP off on one sustained layer | — | — | _new_: PLAYS chips | painted chip | ✅ | e.g. A unlit (a held pad) while B and C arpeggiate |
| 4.5 | Custom Patterns | — | — | _new_: `customRowArea`, browser rows, editor keys | painted list, panels; editor's TIMING/GRID/MAPPING/Transform/··· are PopupMenus; Import is a native dialog | ⚠️ | choose a factory pattern (e.g. "Gallop") and open the editor: ✅; Record a pattern from the keys: ✅; Import a MIDI file: ⛔ native dialog (text only) |
| 4.6 | STRUM | — | — | _new_: `arpPanel.strumKey()`, panel parameters `arp.strum.*` | key, popover, segments, knob | ✅ | **plays only with the Chord pattern** (key shows an unavailable ring otherwise); UP / DOWN / RANDOM / ALT, FREE / SYNC, SPREAD |
| 4.7 | SATELLITE | — | — | _new_: `arpPanel.satelliteKey()`, `layerX.satellite.*` | key, popover, segments, knobs | ✅ | spatial, per layer, works with or without ARP: PAN / ORBIT / BOUNCE, SYNC / FREE, RATE, DEPTH, WIDTH, PHASE, OFFSET |
| 4.8 | SUB | `osp.sub` | `osp.sub.row` | `osp.sub.row`, `osp.sub.enable`, `.level`, `.range` | popover, drag | ✅ | WAVE, OCT, GRIT, LEVEL, RESPONSE FLAT/TAPER |

## Proposed public curriculum

One guide per meaningful task; several guides share one capture (marks). **Pilot** = Gate 1.
Beta = proposed for Gate 3. Later = after beta.

| Guide ID | Title | Steps (IDs) | Capture | Phase |
|---|---|---|---|---|
| `osp.guide.make.load-and-play` | Load a sound and play it | drop-file, wait-ready, play-keys | A | **Pilot** |
| `osp.guide.make.phrase-map-notes` | Turn a phrase into an instrument | drop-phrase, map-notes, play-mapped, open-notes | B | **Pilot** |
| `osp.guide.make.record-input` | Record from an audio input | route-input (host-pending), open-rec, record, stop, play-mapped | C1 | **Pilot** |
| `osp.guide.make.grab-last` | Grab what you just played | play-into-input, open-rec, grab-last, play-mapped | C2 | **Pilot** |
| `osp.guide.make.save-export` | Save a preset or export an instrument | save, export | 1.8 | Beta |
| `osp.guide.play.oneshot-granular` | Switch between One Shot and Granular | open-mode, one-shot, granular | 2.1 | Beta |
| `osp.guide.play.layers` | Layer two or three sounds | add-source, balance, solo, tune | 2.2–2.4 | Beta |
| `osp.guide.play.presets` | Load a preset or starting state | open-library, choose | 2.5 | Beta |
| `osp.guide.synth.blend` | Blend in SYNTH | find, amount, wave, octave, shape | 3.1–3.4 | Beta |
| `osp.guide.synth.imprint` | Colour the oscillator with IMPRINT | mode-imprint, imprint, motion | 3.5 | Beta |
| `osp.guide.arp.basics` | Arpeggiate held notes | open, enable, pattern, rate | 4.1–4.2 | Beta |
| `osp.guide.arp.per-source` | Give each layer its own arpeggio | link-off, edit-b, edit-c, plays-off-a | D (part) | **Pilot** |
| `osp.guide.arp.strum` | Strum a chord | choose-chord, strum-on, open-strum, spread | D (part) | **Pilot** |
| `osp.guide.arp.satellite` | Move each layer in the stereo field | open-satellite, mode, rate | D (part) | **Pilot** |
| `osp.guide.arp.custom` | Use a custom pattern | open-browser, choose-factory, edit | 4.5 | Beta |
| `osp.guide.arp.sub` | Add SUB under your keys | open, on, range | 4.8 | Beta |
| `osp.guide.granular.position-size` | Shape the grains | pos, size, dens | 5.2–5.3 | Beta |
| `osp.guide.granular.key-dna` | Vary the sound from key to key | open, on, draw, amount | 5.4–5.6 | Beta |
| `osp.guide.shape.reimagined` | Reimagine a sound | open-modes, choose, amount | 6.1–6.2 | Beta |
| `osp.guide.shape.movement` | Add MOVEMENT | open, mode, amount | 6.3 | Beta |
| `osp.guide.shape.modulation` | Make a control move with an LFO | lfo-tab, drag-to-knob, depth | 6.5 | Beta |
| `osp.guide.shape.user-macro` | Assign a USER macro | user-bank, right-click, assign, range | 6.6 | Beta |
| `osp.guide.shape.effects` | FILTER, ECHO and SPACE | filter, echo, space | 6.7 | Beta |
| `osp.guide.play.vibrato` | Vibrato from the mod wheel | open-mod, on, depth, raise-wheel | (lesson exists) | Later |
| `osp.guide.shape.shaper` | Use a SHAPER pattern | — | 6.4 | Later |
| `osp.guide.play.midi-learn` | Control OSP from a MIDI controller | right-click, learn, move | PopupMenu | Later |

Reference articles (no video required): Note Atlas / NOTES, CAPTURE page, Arpeggiator (all
parameters), STRUM, SATELLITE, SUB, SYNTH & IMPRINT, KEY DNA, REIMAGINED modes, MOVEMENT modes,
Presets vs exported instruments.

Pilot captures: **A** (load & play), **B** (phrase → MAP NOTES → play → NOTES), **C** (C1 RECORD and
C2 GRAB LAST, one fixture), **D** (three layers; LINK off, per-card patterns, A held; Chord + STRUM;
SATELLITE ORBIT on B). Each pilot capture serves 1–3 guides through its marks.

## Proposed in-plugin changes (separate, need approval; not part of Gates 0–2)

| # | Proposal | Why |
|---|---|---|
| P1 | Lessons: Record & GRAB LAST; per-source ARP (PLAYS / LINK / EDIT); STRUM; SATELLITE; SYNTH & IMPRINT | STATUS NEXT already lists Record and SYNTH; the others have no coverage |
| P2 | Info topics for REC / CAPTURE, SYNTH chip, PLAYS / LINK, STRUM key, SATELLITE key, Custom Patterns | Info covers none of them |
| P3 | Learn targets for the ARP panel's painted parts (the shell already names `arp.cards`, `arp.link`, `arp.card.B.edit`) | would let the public guides and lessons share IDs |
| P4 | No-input hint: match JUCE's standalone labels (**Options → Audio/MIDI Settings...**) | copy currently names "Audio / MIDI settings" |
| P5 | `docs/learn/osp-learn.md` update (15 lessons, 40 topics, 12 cases); `docs/user-guide.md` ARP inset and PLAYS/LINK; stale comments (`InstrumentHost.h` SATELLITE and imprint defaults, `AndorParameters.h` per-card note) | documentation drift |
| P6 | "Learn more online" link from INFO to the public guide (Gate 4) | needs the public URLs first |
| P7 | Byte-identity test for the `phrase` tutorial example | the existing test covers the other five |
