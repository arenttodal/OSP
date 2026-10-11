# ANDOR Drums — plan and status

The specification is the "ANDOR Drums complete V1 build specification" (user, 2026-10-11).
Architecture and identity are in `architecture.md`. This file tracks milestones.

Development branch: `drum-plugin`. The session is pinned to that name, so the requested
`feature/andor-drums` could not be created from here. Base: 31aada0, the tip of the default
branch.

## Milestones

- [x] **M0 Architecture and baseline.** Audit, shared-core plan, identity plan, OSP
      baseline green (unit, integration, regression goldens, performance smoke, plugin),
      buildable `ANDOR_Drums` target (VST3 + Standalone here, plus AU on macOS).
- [x] **M1 Two independent pads** (headless and through the plugin's `processBlock`):
      kick on 36 and snare on 38 answer only their own notes. Both together equal both alone,
      summed (≤ 1e-5). Per-pad gain, tune and mute act on that pad only (the snare is
      sample-identical). Both use OSP's `InstrumentEngine`. OSP's suites pass unchanged.
- [ ] **M2 Eight-pad instrument.** Done: 9 pads, selection, mute/solo, choke groups,
      per-pad gain/pan/tune/start/reverse/envelope, shared effects, session save/load,
      2x4 / 3x3 with a PAD 9 button, pad swap by dragging (undoable), rename (undoable).
      Next: duplicate, clear with undo, kit preset files (`.andorkit`, own schema).
- [ ] **M3 Sample layering.** Multi-file drop choice (round robin / velocity / stack /
      replace), drum sets built with one forced root, round-robin modes (sequential,
      random, shuffle bag), layer editor, undo.
- [x] **M4 Keyboard performance** (V1.1, `v1.1-report.md`): KEY MAP editor (lanes,
      aligned keyboard, zoom/scroll), drag move / resize / root, click-to-assign,
      collision resolution (swap, move others, layer, cancel), MIDI Learn (note, range),
      Auto Map with preview, one undo step per gesture, sustain pedal, state 2.
- [ ] **M5 GUI** (V1.1 layout done: pads on top, equal 2x4 / 3x3 cells, selected-pad
      editor, AMP envelope, effects, kit macros, resizable 0.7-1.5x). Open: Blender knob,
      per-pad modulation sources, a 1x8 pad strip if the mockup's single row is wanted.
- [ ] **M6 Break slicing** (reuse `src/library/Slicer`, onset analysis).
- [ ] **M7 Smart classification** (reuse `src/library/Classifier`; licence review before
      any model weights).
- [ ] **M8 Integration and polish** (portable kits, MIDI Learn, automation, profiling on a
      Mac, hosts).
- [ ] **M9 MACHINE prototype** from existing DRIVE / CHARACTER / ECHO first.

## Needs the user

- Mockups: 2x4, 3x3, selected pad, keyboard mapping and break import, before the final
  pad artwork. Functional work continues meanwhile.
- The Blender knob: Drums picks it up through `OspLookAndFeel` once it is committed.
- DAW checks on a Mac (AU validation, Logic, Live, REAPER).
