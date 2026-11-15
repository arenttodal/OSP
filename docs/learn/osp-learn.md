# ANDOR Learn in ANDOR OSP

_15 Nov 2026. Branch `claude/osp-learn`._

ANDOR OSP's own plugin (`OspAudioProcessorEditor`) now has the shared ANDOR Learn: the menu's
LEARN section, the lesson browser, the ceramic notes, the green highlight, the progress store and
Info. It reuses `shared/andor-learn`, ANDOR ANALOG's framework. The editor is not migrated to the
shell: a small adapter, `apps/plugin/Source/OspLearn.cpp`, provides the lessons, targets, states,
hooks and examples.

## Gate 0: what existed

- **Latest shared Learn.** No "Learn 2.0" is pushed in either repository.
  - The shared framework on every branch is V1 (`49cfb6e`) plus one fix (`52cd128`). It lives in
    OSP's `feature/andor-learn-v1` and in the branch ANDOR ANALOG pins (`feature/aux-live-input`).
  - ANDOR ANALOG's later Learn commits (`0310e99`, `20a7814`, `3e3d305`) change only its own
    lessons and screenshots.
- **The merge.** This branch merges `feature/andor-learn-v1` into OSP's line. The conflicts were
  beside SATELLITE in `ShellProduct.h`, `ShellProcessor.h` and STATUS; both sides were kept.
- **The extension.** The behaviour the specification asks for (no Skip, the Tutorials / Toggle Info
  / Reset Tutorials menu, example preparation with restore, the green highlight) did not exist in
  V1, so it was added to the shared code (below).

## Shared framework changes (`shared/andor-learn`, used by ANDOR ANALOG too)

| Change | Where |
|---|---|
| Menu: a **LEARN** section with **Tutorials…**, **Toggle Info**, a separator, then **Reset Tutorials…**. "Restore Sound Before Tutorial" appears while a tutorial example's previous sound can be restored. The old "Learn ANDOR… / Learn mode / Learn ▸ Reset learning progress" are gone. | `LearnController::addMenuItems` |
| **Reset Tutorials…** asks first, with a ceramic confirmation (Cancel / Reset), and resets only this product's file. | `askToResetProgress`, `NoteKind::confirmReset` |
| **No Skip.** X closes, Back revisits, and Continue (the old Next) follows a read explanation or a done task. "Already done · Continue" covers a task that was already satisfied. `LessonRunner::skip()` stays for API compatibility but no key reaches it. | `refreshNote` |
| **Results that arrive later.** `Completion::waitingForResult()`: an accepted action arms the step, which completes in that step once its postcondition holds. This covers a dropped file loading and analysing. It also completes while the step is paused, because the dropped file hides the drop zone. | `LessonRunner::handle / update` |
| **Lesson readiness.** `Lesson::preparation`: `ready` (checked before the lesson and while it runs; the lesson pauses with `needs`), `exampleId` / `exampleName`, `offerExampleWhen`. | `LessonTypes.h`, `LessonRunner::lessonReady` |
| **Example preparation**, only on the musician's choice. The notes are "Before this lesson" (Not now / Load …), "Use your sound or an example?" (Load … / Use my sound), "Preparing the example" (the lesson starts when the example is playable) and "The example could not be loaded". | `beginLesson`, `loadExampleFor`, `updatePreparing` |
| **Restore.** Closing a lesson whose example was loaded asks "Keep the example sound?" (Restore previous sound / Keep this sound); it never restores silently. The completion note and the menu offer the restore while the product says it is possible. | `offerRestoreIfExample`, `EditorHooks::canRestore / restore` |
| **Parameter gestures as user actions.** `ParameterGestureBridge` reports a gesture of the editor's own controls (begin…end) as `param:<id>`. Host automation, preset recall, undo and the processor's own changes set values without a gesture, so they report nothing. Message thread only. | `LearnActionBridge.*`, `actions::parameterEdited` |
| **The highlight** is green (`#35b264`) with a soft glow: three widening, fading strokes, then a crisp 1.5 px line. Info's hover outline is the same green, quieter. | `HighlightLayer::paint` |
| Browser title **TUTORIALS**; the Info banner reads "Info · Click a control to learn what it does · Esc to exit"; the help notes' meta line is "INFO". | `LearnBrowser`, `LearnController` |
| `startLessonAt (lesson, step)`: starts at a step through its preparation (tests, snapshots). | `LearnController` |
| `EditorHooks`: `productName`, `loadExample`, `exampleReady`, `exampleFailed`, `canRestore`, `restore` (all optional; without them no example is offered). | `LearnController.h` |

**ANDOR ANALOG compatibility.**
- The shell's `LearnTests.cpp` passes in every shell product (`andor_example_tests "[learn]"`,
  7 cases, 97 assertions).
- ANDOR ANALOG pins OSP's `feature/aux-live-input`, so it is unchanged until its pin moves. When
  it moves to a commit with these changes, its own Learn tests will need updating where they
  expect the old Skip key, the old menu labels or "Browse lessons".
- The shell editor's menu now shows the new section.
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

| Group | Lesson (id) | Steps | Example |
|---|---|---|---|
| Start here | Quick Start (`osp.quickstart`) | load (waits for the analysis), play, START, LEVEL, REIMAGINED, back to 0 % | bowed (offered when a sound is loaded) |
| Working with recordings | One Shot & Granular (`osp.playback`) | the playback key, One Shot, Granular, POS, SIZE, DENS, SPREAD, compare | bowed |
| | REIMAGINED (`osp.reimagined`) | find, open (label), KALEIDOSCOPE, TAPE FRAME, TOYBOX, MOSAIC, MIRAGE, amount | bowed |
| | Layering sounds (`osp.layers`) | add (waits), play, B's LEVEL, S on, S off, B's TUNE, different modes, a third layer | layered |
| | Multi-sample instruments (`osp.multisample`) | one sound from many files, play across, open Samples, root, takes and roles, velocity layers | pluck set (needed) |
| Playing and shaping | LIFE: natural variation (`osp.life`) | LIFE 0 %, LIFE up, open, PITCH, TONE, ATTACK, TAKES | pluck (offered) |
| | KEY DNA (`osp.keydna`) | Granular, open, what it does, on, POS depth, SIZE or DENS, AMOUNT | bowed |
| | Arpeggiator (`osp.arp`) | open, on, PATTERN, RATE, OCTAVES, GATE | pluck |
| | Modulation (`osp.modulation`) | LFO 1 tab, SHAPE (SINE), RATE, connect (drag), depth (ring), listen | bowed |
| | Shape the sound (`osp.sound`) | FILTER, DRIVE, MOVEMENT (open), a movement, ECHO, SPACE, compare | bowed (offered) |
| Managing sounds | Presets and instruments (`osp.presets`) | Library, what a preset keeps, Save…, Export instrument…, Open instrument file… | bowed |

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
- SUB (queued: it is not in OSP yet).

## Control-target inventory

Every target is a locator called each time it is needed (nothing raw is stored), and is resolved
against current component bounds.

| Target | Component (bounds) | Shown when | Reveal |
|---|---|---|---|
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

`osp_plugin_tests "[learn]"`: 8 cases, 367 assertions, all passing.

- **Catalog.** It is valid against the editor's targets, has 11 lessons and at least 30 Info
  topics, groups in order, and the new menu labels (the old ones are absent). Learn is idle (no
  component) until asked.
- **Quick Start, end to end.**
  - It starts at the drop zone on an empty instrument, with no Skip key.
  - A real `filesDropped` arms the load step, which completes only after the file has loaded
    and analysed.
  - A processed note completes "play".
  - Host automation does not complete START; the musician's gesture does.
  - LEVEL and REIMAGINED complete through the edited-layer tokens (A's REIMAGINED ID included).
  - "Back to 0 %" needs exactly 0.
  - Progress is "complete" under `osp`, with nothing to restore.
- **Preparation and restore.**
  - The musician's sound is Granular with SPACE at 77 %. The multi-sample lesson asks first, and
    "Not now" changes nothing.
  - "Load" keeps the state before anything is replaced, waits for the set (8 members), and gives
    the lesson a clean instrument.
  - Closing offers the restore. Restoring brings back the voice, SPACE 77 % and Granular.
- **Navigation never changes the sound.** Every lesson is driven with Show me, Continue and Back,
  then X. Every parameter afterwards is identical, and nothing is restorable.
- **Geometry** at 869, 1086 and 1448 px (START, POS, ARP RATE, FILTER, LIFE): the ring equals the
  real control's bounds, the note does not overlap the control and stays inside the window.
- **Reset Tutorials** asks, then resets only OSP's file (ANDOR ANALOG's file is untouched).
  Escape closes the note before the editor's popover.
- **Info** shows no card regions on an empty instrument; FILTER's help reads "FILTER" / "INFO".
- **Tutorial sounds** are written once and are byte-identical when regenerated (same seed).

`[learn-shots]` (hidden; `OSP_LEARN_SHOTS=<dir>`) renders every step of every lesson at 1086 and
869 px, plus the browser, Info, the reset confirmation, preparation and the restore offer.
`docs/learn/screenshots/` keeps the 1086 px set and a few 869 px ones, as JPEG.

## Not verified

- **No real DAW.** Nothing ran in Ableton Live, Logic or any other host. Everything was checked in
  headless tests and offscreen renders on Linux.
  - Real mouse drags (a tab dragged onto a knob, the halo drag) were exercised through the
    editor's own entry points (`dropModulation`, parameter gestures), not through an OS pointer.
  - Keyboard focus and Tab order inside notes were not tried with a screen reader.
- **The tutorial sounds** are synthetic stand-ins. Nobody has listened to them on monitors yet.
- **ANDOR ANALOG** has not been rebuilt against the shared changes (see above).
