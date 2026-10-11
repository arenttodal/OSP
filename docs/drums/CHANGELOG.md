# ANDOR Drums — version history

## 0.1.1 (unreleased, branch drum-plugin): interface and keyboard mapping

- New layout: equal pads on top (2 x 4 or 3 x 3), PAD | EXPANDED and KEY MAP below them,
  the selected pad's sound and AMP envelope, kit effects and macros; resizable 0.7-1.5x.
- KEY MAP editor: every pad on one aligned keyboard; drag zones, edges and roots; click to
  assign; collisions resolved explicitly (swap, move others, layer, cancel); MIDI Learn for
  notes and ranges; Auto Map with preview; zoom and scroll over MIDI 0-127.
- Undo / redo for every map edit, pad swap and rename.
- Pads swap places by dragging their number; sounds and MIDI stay with the pad.
- Per-pad START, REVERSE and envelope parameters; sustain pedal; state schema 2 (pad order).

## 0.1.0 (unreleased, branch drum-plugin)

- First build: a separate plugin (ANDOR Drums, code Adrm, com.osp.andordrums) next to OSP.
- Nine pads, each an OSP instrument engine, sharing one set of OSP effects (DRIVE, MOVEMENT,
  ECHO, SPACE) and a kit volume.
- PAD and EXPANDED keyboard modes with independent maps; zone move, resize, swap and presets
  (engine and tests; the zone editor GUI comes later).
- Choke groups, mono retrigger, one-shot and gate pads; mute and solo.
- Session recall by content hash, exact playback model, missing samples kept for relinking.
- Functional editor in OSP's design language (2 x 4 and 3 x 3 grids).
