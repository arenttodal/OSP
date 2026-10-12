# ANDOR Drums — version history

## 0.1.9 (unreleased, branch drum-plugin): per-pad modulation, drops, OSP update

- **Per-pad modulation:** the MODULATION card gains ENV, LFO, VEL and CTRL tabs (mod wheel,
  aftertouch). Each source goes to up to three of PITCH, LEVEL, PAN, FILTER, START and
  DECAY, with presets: kick pitch drop, harder is brighter or higher, tom bend, moving
  hats, ghost notes. Saved with sessions and kits, undoable, copied by Duplicate.
- **Drops replace at once:** dropping a sample on a pad replaces its sound, with no menu.
  Several files become round robins. Hold Cmd (Ctrl on Windows) while dropping to choose.
- **OSP update merged:** the rendered knobs, the ceramic skin and OSP's final modulation
  system.
- **Specs** for multi-sound kits (several toms and percs on their own keys) and for a drum
  library with pools that cycle or randomise.

## 0.1.8 (unreleased, branch drum-plugin): MACHINE (experimental)

- **MACHINE** in the GLOBAL header: one fader from OFF to STRONG for the character of an
  older drum machine over the whole kit. It brings a lower rate and fewer bits, a gentler
  top end, tape saturation and wear, and more hit-to-hit variation, while tuned drums stay
  in tune. At OFF the kit sounds exactly as before.
- **Fix:** slices and END now stop where they end, also on sounds using a REIMAGINED mode.

## 0.1.7 (unreleased, branch drum-plugin): integration

- **Fix:** the audio callback no longer allocates. Parameter values were looked up by
  building strings on every block; they are now looked up once.
- **Less memory:** about 28 MB less per instance (48 voice slots per pad instead of 208), and
  a quarter of the idle CPU.
- **Tests:** a zero-allocation guard, two instances, sample rates and small buffers, host
  automation, and the performance table.

## 0.1.6 (unreleased, branch drum-plugin): smart classification and import

- **Drop a folder's worth of samples.** IMPORT SAMPLES suggests a category (from the name
  first, then the sound) with a confidence dot. Alike takes are grouped as round robins and
  placed on matching pads. Overflow goes to a tray. Broken files and duplicates are listed.
- **Corrections:** own pad, move, group, destination, category, reset. Import is one undo
  step.
- **Breaks:** slices are classified and grouped onto the kick, snare and hat pads.
  Strongly varying hits become velocity layers, so ghost notes are kept.

## 0.1.5 (unreleased, branch drum-plugin): break slicing

- **BREAK:** drop a break and SLICE BREAK opens. Its markers can be moved, added and
  deleted, and its sensitivity and minimum gap adjusted. Every slice can be heard.
- **The slice tray:** drag slices onto pads, or assign them; set categories.
- **Apply:** as a new kit, into the empty pads, or merged as round robins. It is undoable.
- **No audio is copied:** each slice is a region of the one break.
- **The groove as MIDI:** drag it into the DAW or save it. It is kept with the kit.

## 0.1.4 (unreleased, branch drum-plugin): kits

- **Duplicate to** on every pad's menu: copies the layers, name and settings, and keeps the
  target's note. One undo step.
- A kit menu by the kit name, with these commands:
  - new kit and open kit, each undoable;
  - save and save as (`.andorkit`, own schema; OSP files are refused);
  - export a portable kit: a folder with the samples, no private paths;
  - find missing samples in a folder: by content hash only, including renamed files.
- **Relink...** on a missing layer.

## 0.1.3 (unreleased, branch drum-plugin): sample layers

- Several samples on one pad, from a multi-file drop or **Add Layers...**. They play as round
  robin (shuffle, sequential or random), as velocity layers (sorted soft to hard, keeping
  their recorded levels), or as a stack.
- A layer editor in the selected pad:
  - a list with audition, a last-played mark, rename, drag to reorder, duplicate and remove;
  - the selected layer's level, velocity range, and START/END on the waveform.
- Every layer edit is undoable. Sessions save every layer (state 3), and older sessions open
  unchanged.

## 0.1.2 (unreleased, branch drum-plugin): Warm Ceramic + Teak, the mockup's layout

- The look of ANDOR/OSP's direction 09: teak cheeks, ceramic housing and cards, A1 ivory
  keys, B1 tabs, the C1/T3 volume fader, glass lens LEDs, and OSP's rendered Blender macro
  knob for all macro knobs.
- Layout after the mockup: one row of 8 pads (2 x 4 and 3 x 3 still available; the window
  grows for them), the selected pad's sound with a draggable START, an always-visible
  KEYBOARD RANGE strip (root note, zone presets), AMP / EFFECTS / GLOBAL below.
- Kit name (saved), output meter, effect power keys.
- Keyboard range colours per key: tints stay inside each key (no spill onto black keys or
  other pads' keys), with a slim cap and the pad number above each pad's keys.

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
