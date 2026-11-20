# ANDOR OSP changelog

Each release has a `## <version>` section; the release script copies that section into the
release's `RELEASE_NOTES.md` and refuses to build a version without one. Newest first.

## 0.9.0-beta.1

The first signed and notarized beta of ANDOR OSP, for invited testers (November 2026).

**Install**
- A standard macOS installer: Audio Unit, VST3 and the standalone app, each optional
  (Customize). Apple silicon and Intel, macOS 11 or later.
- Signed with Developer ID and notarized by Apple: it opens like any commercial plug-in, with
  nothing to override in macOS security settings.
- Your presets, samples, packs, patterns, preferences and DAW projects are not touched.
  Projects saved with earlier OSP builds open with this one.
- Had an earlier test build? The installer can move it aside (Customize › "Move earlier OSP
  test builds aside"), so your DAW finds only this version. Nothing is deleted.

**What is in this beta**
- One tonal recording becomes an expressive, playable instrument: three sources (A, B, C),
  the engine's continuation, dynamics and motion, SUB, SATELLITE, FILTER, the CORE and USER
  macros, PITCH / MOD wheels and ADVANCED.
- Effects: SPACE, ECHO and MOVEMENT racks, the FX ROUTING view and its mixer.
- Arpeggiator: eighteen styles, per-card lanes, STRUM for CHORD steps, and **custom
  patterns**: record a pattern from the keys, import one from a MIDI file, or start from a
  factory pattern; keep it in the session, in presets and in your pattern library.
  - Choose one from **Custom Patterns ›** at the end of the style list. The pencil beside the
    pattern's name edits it.
  - Shape it right on the grid: drag a note up or down, drag its edge, double-click a rest for
    a note, right-click for more.
  - Change how the take is read (TIMING, MAPPING); the editor asks before replacing your edits,
    and everything can be undone.
  - Preview it, then Apply Pattern. Closing with changes you have not applied or saved asks
    first.
- PEDAL LOOP with forward and ping-pong loops; Note Atlas multi-sample sets; live capture
  (RECORD and GRAB from your audio input).
- A phrase you drop, RECORD or GRAB becomes a NOTES instrument whose FILL reaches ±24
  semitones (was ±12): a short phrase plays across four octaves. Instruments you saved keep
  their own reach.
- NOTES DETECTION **FEWER** now makes every note at least 0.4 s long: a shorter note joined to
  its neighbour becomes part of it (nothing is cut away), and a short note with silence on both
  sides stays in the map, switched off, for you to switch on. NORMAL and MORE are unchanged.
- **DAMPEN** is on by default: in a new instance and in every Starting State, on all three
  cards. Projects and presets you saved keep their own setting.
- **MIDI Learn:** right-click a control, choose **Learn MIDI CC**, move a knob or fader on your
  controller: it now turns that control (the knob moves, and your DAW can record it). Forget it
  from the same menu. Your controller setup is saved with the project and stays when you change
  presets.
- The Library and preset browser, seven built-in Starting States, ANDOR Learn lessons and
  tutorials.

**Known issues:** see KNOWN_ISSUES.md.
