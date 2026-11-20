# Proposed work item: in-plugin Learn and INFO for the beta (not part of the capture project)

_Status: proposal, 20 Nov 2026. Owner priority: essential for beta (MIDI Learn can follow).
To be done as its own reviewed change to the shipping plugin, on its own branch, separate from
`feature/learn-capture-studio`. Nothing here is implemented._

## Why

Gate 0 found that OSP's Learn 2.0 (15 lessons, 40 INFO topics, `apps/plugin/Source/OspLearn.cpp`)
has no lesson, no semantic target and no INFO topic for seven of the newest features. The public
`/learn` guides do not replace in-plugin help: the plugin guides people through the real controls
while they play.

## Scope

| Feature | Lesson (proposed id) | INFO topics | Learn targets / states to add |
|---|---|---|---|
| RECORD / GRAB LAST | `osp.capture` "Record or grab from an input": open REC, RECORD/STOP or GRAB LAST, play the mapped notes | REC key, CAPTURE page (INPUT, RECORD, GRAB LAST) | `osp.source.rec`, `osp.capture.page`, `.record`, `.grab`, `.meter`; states `osp.capture.open`, `osp.capture.recording`, `osp.capture.connected` |
| Per-source ARP | `osp.arp.layers` "Each layer its own arpeggio": LINK off, EDIT B/C, PLAYS | PLAYS, LINK, EDIT | `osp.arp.cards` (A/B/C chips), `osp.arp.link`, `osp.arp.edit.{A,B,C}` (the shell already names `arp.cards`, `arp.link`, `arp.card.B.edit`) |
| Custom Patterns | `osp.arp.custom` "Use a custom pattern": open Custom Patterns ›, choose a factory pattern, open the editor | Custom Patterns row, pencil, editor | `osp.arp.custom.row`, `.pencil`, `.browser`, `.editor` |
| STRUM | `osp.arp.strum` "Strum a chord": Chord, STRUM on, direction, SPREAD | STRUM key (including "plays only with Chord") | `osp.arp.strum`, `.light`, `.panel` |
| SATELLITE | `osp.satellite` "Move each layer in the stereo field": open, on, mode, RATE | SATELLITE key and panel | `osp.arp.satellite`, `.light`, `.panel` |
| PEDAL LOOP | `osp.pedalloop` "Hold a note with the pedal": LOOP off, Ping-Pong, play, pedal | the playback key's PEDAL LOOP section | `osp.source.mode` exists; `osp.source.loop` (the card's LOOP key) |
| SYNTH / IMPRINT | `osp.synth` "Blend in SYNTH and IMPRINT": the chip, AMOUNT, WAVE, SHAPE, IMPRINT, MOTION | SYNTH chip, SYNTH page | `osp.source.synth`, `osp.synth.page`, `.mode`, `.wave`, `.shape`, `.imprint`; state `osp.synth.open` |
| MIDI Learn (after beta) | — | the right-click MIDI LEARN section | — |

Each lesson follows the existing rules: interface only, a user-origin action plus a verified
result per task step, Use Current Sound / Load Example Sound, the requirements system, and
the `[learn]` suite (catalog validity, geometry at 869 / 1086 / 1448 px, navigation never changes
the sound).

## Also in this work item

- **Verify on macOS: a click on "Custom Patterns ›" may not open the browser.** In the capture
  environment (real X server events) a click on the row opened the browser and the editor's
  outside-click watcher closed it again at once, with a quick click and with a long one. The code
  shows why: `openCustomBrowser()` anchors the popover to an invisible `browserAnchor`, and
  `mouseDownAnywhere` closes any popover whose anchor does not contain the pressed component
  (`PluginEditor.cpp` ~177, `ArpSection.cpp` 817-832). The R2 tests open the browser by calling
  `openCustomBrowser()` directly, so they would not catch it. If confirmed, beta testers cannot reach
  Custom Patterns from the list. The pencil in the pattern field works.
- **Verify on macOS: the first click on ••• after saving a preset is ignored.** In the capture
  environment, after Save to Library closed the Library, one click on the ••• key opened nothing; a
  second click a second later opened the menu (reproduced twice with scenario
  `learn/capture/tests/diag-menu-after-save*.json`). May be window-focus behaviour of the test
  environment; check in the standalone app.
- **Pedal Loop has no lesson or INFO topic of its own** (only a sentence in the playback mode's
  INFO text). Add it to the playback lesson or as its own short lesson.

- **UI verification item: the CAPTURE page's tab label.** In the headless Linux render the
  "CAPTURE A" label in the page's top-left looks overdrawn by the card's tool icons
  (`[capture-shots]` `capture-1-recording.png`; the same frame in the Gate 1 RECORD capture).
  Check it in the standalone app on macOS before classifying it as a defect.
- **Copy fix:** the no-input hint says "Choose an audio input in Settings (the menu: Audio / MIDI
  settings)"; JUCE's standalone window shows **Options → Audio/MIDI Settings...**.
- **Docs:** update `docs/learn/osp-learn.md` (15 lessons, 40 topics, test counts) and
  `docs/user-guide.md` (the ARP key no longer shows the pattern/rate inset; PLAYS / LINK / EDIT).
- **Byte-identity test** for the `phrase` tutorial example (the other five have one).
- **Shared IDs:** use the same target IDs the public guides use (`docs/learn-studio/
  content-and-scenario-model.md`), so a later INFO → public guide link (Gate 4) is a lookup.

## Dependencies and risks

- Most of the ARP panel is painted, so its targets need part-bounds locators (the panel already
  exposes `cardChipArea`, `linkArea`, `editTabArea`, `customRowArea`, `strumKey`, `satelliteKey`).
- ANDOR ANALOG shares `shared/andor-learn` and the ARP panel: any shared change must keep its
  Learn tests and visual regression unchanged (OSP's "both instruments" rule).
- Host routing for the Sidechain input is still unverified on macOS; the capture lesson's
  routing text stays generic until it is.

## Estimate

4–6 focused sessions: one per feature lesson with its INFO topics and targets, plus tests and the
ANDOR ANALOG check.
