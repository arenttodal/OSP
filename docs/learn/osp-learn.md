# ANDOR Learn in ANDOR OSP

_15 Nov 2026. Branch `claude/osp-learn-2` (supersedes `claude/osp-learn`, which built on V1)._

ANDOR OSP's own plugin (`OspAudioProcessorEditor`) now uses **ANDOR Learn 2.0**, the shared
framework in `shared/andor-learn` that ANDOR ANALOG uses. OSP has no copy or parallel version of
it. The editor is not migrated to the shell. A small adapter, `apps/plugin/Source/OspLearn.cpp`,
gives the shared controller OSP's lessons, targets, states, readiness checks, example sounds and
Info.

## Gate 0: what existed, and the merge

- **Learn 2.0** is on OSP's `feature/aux-live-input` (`da0e75c`, the line ANDOR ANALOG pins at
  `be5f45f`). This branch merges that line, up to `938344d`, into OSP's default line.
- **What the merge brought besides Learn 2.0:**
  - the shell's inline card arpeggiator lanes and arpeggiator follower (from that line);
  - user macros (`UserMacroBank`), effective-value displays (`src/engine/EffectiveValues.h`) and
    their tests.
- **Conflicts.**
  - The shell files take that line's version.
  - OSP's own processor keeps its `CardArpLanes` and SATELLITE.
  - `InstrumentHost` keeps both sides' additions.
  - The FILTER display name and the SATELLITE click fix are kept on both sides.
- **The V1 extensions are gone.** `claude/osp-learn` had added Learn behaviour to V1: no Skip, the
  new menu, preparation, restore and the green outline. Learn 2.0 does all of that itself, so
  those changes were dropped in favour of 2.0's.
- **One shared addition stays:** `ParameterGestureBridge` (below). ANDOR ANALOG does not use it,
  and it changes nothing there.

## Learn 2.0 as OSP uses it

| Learn 2.0 piece | In OSP |
|---|---|
| Menu: one **Learn** submenu (Tutorials…, Toggle Info, Restore Previous Sound while possible, Reset Tutorials…) | added to the ••• menu by `LearnController::addMenuItems` |
| No Skip. X closes and never restores; Back; Next / Continue once the task is done or already so | unchanged |
| `Step::requirements` block a step with a reason, the control that fixes it (outlined) and the lesson's example | `soundReady()`, `granular()`, `twoLayers()`, `sampleSet()` |
| Set-up step: **Use Current Sound** / **Load Example Sound** | every lesson but Quick Start (`setUp`), whose first step is the load itself |
| `Lesson::exampleId` and `EditorHooks::loadExample / canRestoreSound / restoreSound` | `OspAudioProcessor::loadTutorialExample`, `canRestoreBeforeTutorial`, `restorePreviousState` |
| `Completion::viaMenu()` for dropdown choices | the playback key, REIMAGINED and MOVEMENT modes, LFO SHAPE, ARP PATTERN |
| Green outline, ceramic notes, TUTORIALS browser, Info, per-user progress (`osp.json`) | unchanged |

**`ParameterGestureBridge`** (`LearnActionBridge.*`, shared, used only by OSP so far). OSP's
editor is not the shell, so the shell's touch reporting does not reach its controls. The bridge
reports a gesture of the editor's own controls (begin…end) as `param:<id>`, on the message thread.
Host automation, preset recall, undo and the processor's own changes set values without a
gesture, so they report nothing. The adapter also reports the edited layer's `layer{L}.…` alias
(below) for each such gesture.

**ANDOR ANALOG compatibility.**
- The shell's own Learn tests (`tests/shell/LearnTests.cpp`, Learn 2.0's) run unchanged in every
  shell product.
- ANDOR ANALOG pins `feature/aux-live-input`, so this branch changes nothing for it until its pin
  moves. The only shared Learn file changed is the bridge: an addition, not used by the shell.
- **Not done here:** building ANDOR ANALOG against this branch.

## Shared UI changes (all opt-in or inert for other callers)

- `EngineCard`: accessors `cardKnob(i)`, `granularKnob(i)`, `muteKey()`, `soloKey()` and
  `layerMenuKey()`.
- `KeyDnaEditor::onUserEdit`: reports the musician's committed edits (power, a tab, depth,
  variation, curve, drawn keys, individuality).
- Learn finds three selectors by their parameter, so they gained binding tags:
  - `MacroPanel::mode` selectors (REIMAGINED, MOVEMENT, DRIVE, FILTER…)
  - LIFE's CHARACTER, TAKES and ORDER
  - the LFO's SHAPE

## OSP-specific code

- `apps/plugin/Source/OspLearn.cpp` holds the catalog (11 lessons, 32 Info topics), the targets,
  the states, `createLearn` / `tickLearn` / `learnAction` and the click hooks.
- **Click hooks.** The adapter wraps the existing callbacks: card KEY DNA and REIMAGINED, the
  macro labels, ARP's toggle, the mod tab, a mod drop, the preset bar's Library and the drop
  zone's example. Hooks were added in `filesDropped`, `chooseFile`, "Samples", "Library…", Cmd+L
  and preset or instrument save and open. A reveal (Show me) never reports an action.
- **Notes played.** A note played is the processor's note counter, polled on the editor's timer.
- **Edited-layer tokens.** `layer{L}.…` in a lesson is the edited layer's parameter, and
  `layer{L}.reimagined` is `reimagined` for layer A.
- **Tutorial sounds.** `apps/plugin/Source/TutorialSounds.*` writes five examples from fixed
  seeds, so they are ours to ship and identical everywhere. They go once to
  `<user application data>/ANDOR/OSP/Tutorial Sounds` (`OSP_TUTORIAL_DIR` overrides it) and load
  through the normal sample path, so a session recalls them like any recording.
  - **bowed:** C3, a harmonic series through body resonances, a bowed attack, delayed vibrato and
    bow noise.
  - **pluck:** A3 (Karplus-Strong).
  - **voice:** A3 (formant vowel).
  - **layered:** bowed plus voice.
  - **pluckSet:** G3, C4, G4 and D5, two takes each, as one multi-sample layer.

  They are synthetic; the lessons call them "the bowed note" and so on, never recordings of real
  instruments.
- `OspAudioProcessor::loadTutorialExample`:
  - It keeps the whole state first (`previousState`, labelled "Before the tutorial example").
  - It then clears the layers, resets the settings and loads the example. Undo history is kept.
  - `tutorialExampleReady / Failed` report when every example layer is ready and playable.
  - `canRestoreBeforeTutorial()` is true only while the kept state is still the tutorial's: any
    later preset load replaces it.

**Nothing on the audio thread.** Learn is message-thread only. It has no parameter, adds nothing
to presets or sessions (progress is a per-user file), and no DSP file changed.

## The curriculum

| Group | Lesson (id) | Steps (after the set-up step) | Example | Required |
|---|---|---|---|---|
| Start here | Quick Start (`osp.quickstart`) | load (its set-up: waits for the analysis), play, START, LEVEL, REIMAGINED, back to 0 % | bowed | a playable sound |
| Working with recordings | One Shot & Granular (`osp.playback`) | the playback key, One Shot, Granular, POS, SIZE, DENS, SPREAD, compare | bowed | a sound; Granular for POS…SPREAD |
| | REIMAGINED (`osp.reimagined`) | find, open (label), KALEIDOSCOPE, TAPE FRAME, TOYBOX, MOSAIC, MIRAGE, amount | bowed | a sound |
| | Layering sounds (`osp.layers`) | add (waits), play, B's LEVEL, S on, S off, B's TUNE, different modes, a third layer | layered | a sound; two layers after "add" |
| | Multi-sample instruments (`osp.multisample`) | one sound from many files, play across, open Samples, root, takes and roles, velocity layers | pluck set | a multi-sample layer |
| Playing and shaping | LIFE: natural variation (`osp.life`) | LIFE 0 %, LIFE up, open, PITCH, TONE, ATTACK, TAKES | pluck | a sound |
| | KEY DNA (`osp.keydna`) | Granular, open, what it does, on, POS depth, SIZE or DENS, AMOUNT | bowed | a sound; Granular after its step |
| | Arpeggiator (`osp.arp`) | open, on, PATTERN, RATE, OCTAVES, GATE | pluck | a sound |
| | SUB (`osp.sub`, added with SUB: docs/sub/osp-sub.md) | open (the housing's SUB row), on (its light), hold a chord, LEVEL, range | bowed | none (SUB plays without a recording) |
| | Modulation (`osp.modulation`) | LFO 1 tab, SHAPE (SINE), RATE, connect (drag), depth (ring), listen | bowed | a sound |
| | Shape the sound (`osp.sound`) | FILTER, DRIVE, MOVEMENT (open), a movement, ECHO, SPACE, compare | bowed | a sound |
| Managing sounds | Presets and instruments (`osp.presets`) | Library, what a preset keeps, Save…, Export instrument…, Open instrument file… | bowed | a sound |

**Set-up and requirements.** Every lesson but Quick Start opens with a set-up step: **Use
Current Sound** or **Load Example Sound**. Nothing is loaded unless the musician chooses it. A
later step whose requirement fails (no sound, not Granular, fewer than two layers, not a set) is
blocked. Its note says what is missing and outlines the control that fixes it, where there is
one. It also offers the example, except for Granular, which is one click on the layer's own
playback key.

**Completion rules.**
- Every task step needs a user-origin action, either a parameter gesture or a control's own click
  handler, and then a verified result.
- Explanation steps (manual) cover these:
  - what a preset keeps;
  - the third layer;
  - the Samples inspector's root, roles and velocity layers (changing a correct mapping only to
    finish a lesson would harm the instrument);
  - Export and Open instrument (writing a file only to finish a lesson is not asked).

**Not taught, on purpose:**
- Analog's per-card PLAYS / LINK. They exist in OSP's arpeggiator, but the specification keeps
  them out.
- SHAPER DNA.

## Control-target inventory

Every target is a locator called each time it is needed (nothing raw is stored), and is resolved
against current component bounds.

| Target | Component (bounds) | Shown when | Reveal |
|---|---|---|---|---|
| `osp.header.preset` / `.menu` / `.mix` | preset bar / ••• key / header MIX | always / always / two layers | — |
| `osp.source.dropZone` | the empty drop zone | no layer | — |
| `osp.source.card` | the edited layer's card | a layer | — |
| `osp.source.{start,tune,pan,level,reimagined}` | the edited card's knob | a layer | — |
| `osp.source.reimagined.label` | the REIMAGINED caption's bounds | a layer | — (opening is the task) |
| `osp.source.mode` | the playback key (top right of the card) | a layer | — |
| `osp.source.{eq,notes,mute,solo,menu,root}` | EQ, NOTES (Atlas only), M+S, S, ⋯, the header's root text | a layer | — |
| `osp.source.{A,B,C}[.start …, .mode, .solo, .mute]` | that layer's card and controls | that layer shown | — |
| `osp.granular.{position,size,density,tune,spread}` | the granular strip's knobs | GRANULAR and the strip shown | shows the strip (kept shown during the step) |
| `osp.source.keyDna` | the KEY DNA key in the strip | GRANULAR and the strip shown | shows the strip |
| `osp.macro.{life,drive,filter,movement,space,echo}` (+ `.label`) | knob and name (+ name only) | always | — |
| `osp.macros` | the macro row | always | — |
| `osp.life.{pitch,tone,attack,takes}` | LIFE popover controls (by parameter) | LIFE popover open | opens it |
| `osp.movement.mode`, `osp.reimagined.mode` | the popover's mode selector (by parameter) | that popover open | opens it |
| `osp.arp.{control,enable,panel,pattern,rate,octaves,gate}` | ARP key, its light, the panel, the pattern field+list, the shown RATE knob, the octave keys, GATE | panel open; PATTERN/RATE/OCTAVES while A's are edited | expands the panel (and EDIT A) |
| `osp.mod.panel`, `osp.mod.lfo1` | the panel, LFO 1's tab | always | — |
| `osp.mod.lfo1.{shape,rate}` | LFO page controls (by parameter; RATE or its synced division) | LFO 1's page | shows the LFO 1 page |
| `osp.mod.halo` | the control LFO 1 is routed to (its bounds + 6 px: the ring) | a route from LFO 1 | — |
| `osp.keydna.{graph,power,depth,variation,amount,tabs}` | KEY DNA editor parts (`controlArea`) | KEY DNA open | opens KEY DNA |
| `osp.samples[.role/.root/.layer]` | the Samples inspector (its first file's lists) | a multi-sample layer, inspector open | shows the inspector |
| `osp.library[.save]` | the Library window / its Save… key | Library open | opens the Library |
| `osp.keyboard`, `osp.advanced` | keyboard / Advanced card | always | Advanced opens its popover |

**States checked:**
- `osp.source.ready` (the edited layer is loaded and playable), `osp.source.empty`,
  `osp.layers.two`, `osp.layers.fewerThanTwo`, `osp.source.set`, `osp.source.granular`;
- `osp.popup.{reimagined,life,movement}.open`, `osp.keydna.open`, `osp.keydna.enabled`,
  `osp.arp.panel.open`, `osp.mod.tab.lfo1`, `osp.mod.lfo1.routed`, `osp.samples.open`,
  `osp.library.open`.

## Editorial verification

Each sentence was checked against the code as it is now (the fact sheet behind it lists file and
line). The main checks:

- **REIMAGINED at 0 %.** No mode engine starts, and KALEIDOSCOPE's extras are zero
  (`InstrumentVoice.cpp`, `KaleidoscopeEngine.h`). Hence "At 0 % the layer plays without it", and
  not "plays the original recording": LIFE, sustain and the effects still apply.
- **The mode texts** follow each engine's header:
  - TAPE FRAME: tape speed, so lower notes play slower and darker; AGE adds wear, STABILITY adds
    wow and flutter.
  - TOYBOX: a low-resolution memory; PLAY selects FWD / TURN / CHAOS.
  - MOSAIC: harmonic frames at the played pitch; MOTION moves through them.
  - MIRAGE: a variable clock into a resonant low-pass FILTER.
- **The playback key** is a symbol at the top right of the card. Its menu reads "One Shot" /
  "Granular", as written, not "ONE SHOT".
- **SIZE** is 20–400 ms, **DENS** is grains per second, and **SPREAD** is the reach around POS
  (`AndorParameters`).
- **LIFE TAKES** keeps a fixed number of synthetic *performances* per note, so it works with one
  file. The lesson says so and says LIFE does not create new recordings.
- **KEY DNA** offsets the granular settings per key at note-on, and only in Granular. The lesson
  says One Shot is not affected.
- **No "Add Layer" on a card.** "Add layer…" is in the ••• menu, or appears as a drag target
  (ADD LAYER) while a file is dragged.
- **The modulation tabs** are AMP, ENV, LFO 1, LFO 2, VEL and NOTE. A dragged tab makes a route
  at 50 % depth, and the ring edits the depth.
- **FILTER** is the `character` macro: its cutoff runs between MIN and MAX. DRIVE at 0 % is out of
  the signal path. ECHO's types are TAPE, BBD and ASCEND. SPACE's are ROOM, HALL, PLATE and
  SPRING, plus ABYSS, VINTAGE and BLOOM.
- **Presets** store no audio. They reference recordings in OSP's sample store; an exported
  `.ospinstrument` carries copies of the recordings.
- **Velocity layers** come only from dynamics words in the file names (the automatic mapping
  policy). Takes of one pitch take turns.

**Avoided claims.** The lessons do not say:
- that the root is always right;
- that every mode suits every source;
- that LIFE creates round robins;
- that Granular is smoother;
- that a parameter changes the recording.

## Tests

`osp_plugin_tests "[learn]"`: 8 cases, 438 assertions, all passing.

- **Catalog.**
  - It is valid against the editor's targets.
  - It has 11 lessons and at least 30 Info topics, with the groups in order.
  - The menu has Learn 2.0's labels.
  - Learn is idle (no component) until asked.
- **Quick Start, end to end.**
  - On an empty instrument it starts at its set-up step at the drop zone, with no Skip key.
  - A real `filesDropped` reports the drop. The step completes only once the recording has loaded
    and been analysed.
  - A processed note completes "play".
  - Host automation and a preset recall do not complete START; the musician's gesture does.
  - LEVEL and REIMAGINED complete through the edited-layer tokens (A's REIMAGINED ID included).
  - "Back to 0 %" needs exactly 0.
  - Progress is "complete" under `osp`, with nothing to restore.
- **Set-up and restore.**
  - The musician's sound is Granular with SPACE at 77 %. The multi-sample lesson's set-up step
    says the sound is not a set and changes nothing.
  - X there leaves nothing to restore.
  - **Use Current Sound** blocks the next step and says what is missing.
  - **Load Example Sound** keeps the whole state first, waits for the set (8 members), and gives
    the lesson a clean instrument.
  - X does not restore. The menu's **Restore Previous Sound** brings back the voice, SPACE 77 %
    and Granular.
- **Navigation never changes the sound.** Every lesson is driven with Use Current Sound, Show me,
  Continue and Back, then X. Every parameter afterwards is identical, and nothing is restorable.
- **Geometry** at 869, 1086 and 1448 px (START, POS, ARP RATE, FILTER, LIFE): the outline equals
  the real control's bounds, and the note does not overlap the control and stays inside the window.
- **Reset Tutorials** asks, then resets only OSP's file (ANDOR ANALOG's file is untouched).
  Escape closes the note before the editor's popover.
- **Info** shows no card regions on an empty instrument. FILTER's help reads "FILTER" / "INFO".
- **Tutorial sounds** are written once and are byte-identical when regenerated (same seed).

`[learn-shots]` (hidden; `OSP_LEARN_SHOTS=<dir>`) renders every step of every lesson at 1086 and
869 px, plus the browser, Info, the reset confirmation, a set-up step, a blocked step and a
loaded example (182 images).
`docs/learn/screenshots/` keeps the 1086 px set and a few 869 px ones, as JPEG.

## Not verified

- **No real DAW.** Nothing ran in Ableton Live, Logic or any other host. Everything was checked in
  headless tests and offscreen renders on Linux.
  - Real mouse drags (a tab dragged onto a knob, the halo drag) were exercised through the
    editor's own entry points (`dropModulation`, parameter gestures), not through an OS pointer.
  - Keyboard focus and Tab order inside notes were not tried with a screen reader.
- **The tutorial sounds** are synthetic stand-ins. Nobody has listened to them on monitors yet.
- **ANDOR ANALOG** has not been rebuilt against the shared changes (see above).
