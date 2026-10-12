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
- [x] **M2 Eight-pad instrument.** Done:
      - 9 pads, selection, mute/solo, choke groups;
      - per-pad gain/pan/tune/start/reverse/envelope, shared effects, session save/load;
      - 1x8 / 2x4 / 3x3 with a PAD 9 button;
      - pad swap by dragging (undoable), rename (undoable);
      - duplicate pad (undoable), clear with undo;
      - `.andorkit` kits with their own schema, portable kit export, finding and relinking
        missing samples by content (`m2-kits.md`).
- [x] **M3 Sample layering** (`m3-layers.md`). Done:
      - the multi-file drop choice (round robin / velocity / stack / replace);
      - 1–16 layers per pad, each at its own natural pitch;
      - orders: sequential, random without repeats, shuffle bag;
      - velocity ranges, with recorded levels kept;
      - the layer editor: list, audition, rename, reorder, duplicate, remove, level,
        velocity range, START/END;
      - one undo step per edit;
      - state 3.
- [x] **M4 Keyboard performance** (V1.1, `v1.1-report.md`): KEY MAP editor (lanes,
      aligned keyboard, zoom/scroll), drag move / resize / root, click-to-assign,
      collision resolution (swap, move others, layer, cancel), MIDI Learn (note, range),
      Auto Map with preview, one undo step per gesture, sustain pedal, state 2.
- [ ] **M5 GUI** (V1.1 layout done: pads on top, equal 2x4 / 3x3 cells, selected-pad
      editor, AMP envelope, effects, kit macros, resizable 0.7-1.5x). Open: Blender knob,
      per-pad modulation sources, a 1x8 pad strip if the mockup's single row is wanted.
- [x] **M6 Break slicing** (`m6-breaks.md`). Done:
      - a multi-band spectral-flux slicer, validated on a synthetic collection;
      - SLICE BREAK view: markers, edits, audition, tray, drag to pads;
      - apply as a new kit, into empty pads, or merged;
      - slices are non-destructive layers of one sound;
      - the groove is kept and exported as MIDI.
- [x] **M7 Smart classification** (`m7-classification.md`). Done:
      - classification by name and sound, with confidence, Unknown and layered hits;
      - similarity grouping and auto-mapping with an overflow tray;
      - the import preview with corrections and undo;
      - break slices classified and planned by category;
      - licence review. No model weights are shipped: none is cleared, and CLAUDE.md
        rule 5 applies.
- [x] **M8 Integration and polish** (`m8-integration.md`). Done:
      - a zero-allocation proof of processBlock, which found and fixed a per-block
        allocation;
      - two instances, sample rates and small buffers, host automation;
      - measurements for every scenario in 11.5;
      - voice memory per pad cut from 208 to 48 slots.

      Needs a Mac: worst-case timing and host checks.
- [x] **M9 MACHINE prototype** (`m9-machine.md`). Done:
      - one control, built only from existing OSP processing (TOYBOX's digital character,
        the CHARACTER filter, tape DRIVE and MOVEMENT, LIFE);
      - bit-identical at 0; pitch within a few cents;
      - END now works for REIMAGINED mode voices.

      Open: the A/B listening test to tune the curve; timing imperfection is deliberately
      left out.

## Needs the user

- Mockups: 2x4, 3x3, selected pad, keyboard mapping and break import, before the final
  pad artwork. Functional work continues meanwhile.
- The Blender knob: Drums picks it up through `OspLookAndFeel` once it is committed.
- DAW checks on a Mac (AU validation, Logic, Live, REAPER).
