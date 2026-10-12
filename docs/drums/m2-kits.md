# ANDOR Drums M2 (finished): duplicate pads, kit files, portable kits, missing samples

Completes Milestone 2 (kit save/load) and the pad interactions in spec §4.4 (duplicate pad).
It also covers Part 10 (kit presets §10.2, portable kits §10.3, sample availability §10.4,
own schema §10.5).

## Duplicate pad

The pad menu (on the pad or the selected-pad gear) has **Duplicate to**: the next empty pad,
or any other pad, which then shows "(replaces it)".

- **What is copied:**
  - every layer (the sounds are shared, so nothing is reloaded);
  - the name, with " copy" added when the pad was named;
  - the category, choke group, voice and trigger modes;
  - every per-pad parameter except SOLO.
- **What is not copied:** each pad keeps its own MIDI note and EXPANDED zone, because
  duplicating never re-maps the keyboard.
- One undo step restores the target as it was, parameters included.

## Kits (`.andorkit`)

The **kit menu** (≡ at the right of the kit name) has:

- New kit;
- Open kit...;
- Save kit;
- Save kit as...;
- Export portable kit...;
- Find missing samples (n)....

A kit file is XML: `<AndorDrumsKit kitSchemaVersion="1" name saved [portable]>`, with the
same children as the session state (schemaVersion 3, `architecture.md` §4). That is
everything §10.2 lists:

- the pad layout, pad names, sample references, layers and layer modes;
- the PAD map and EXPANDED zones with their roots;
- choke groups, envelopes, effects and macros;
- the keyboard mode;
- the kit name and save date.

Modulation is stored as far as Drums has it (the kit macros). How kits are handled:

- **New kit and Open kit are each one undo step.** The step holds the whole kit before and
  after; undo restores it, and the sounds come back from the sample store.
- **Never an OSP file.** Any other root tag (OSP's `OSP` session or preset) is refused with a
  message. A kit from a newer `kitSchemaVersion` is refused, not misread (tested).

## Portable kits (§10.3)

**Export portable kit...** writes `<folder>/<Kit name>/<Kit name>.andorkit` and a `Samples/`
folder:

- **One copy per sound,** by content. Two different sounds with the same file name are kept
  apart: the second gets the start of its hash as a prefix.
- **No private paths.** Each sound is referenced by `kitPath="Samples/…"`. The absolute
  original path is left out.
- **On another computer,** a sound loads from the sample store by hash. Failing that, it
  loads from its original path, and failing that, from the kit's own copy.
- **Bit-identical.** A kit opened that way plays exactly as the original (tested with an
  empty store, the originals deleted, and the kit folder moved).
- **Ordinary saves copy nothing.** Sessions and plain kit saves only reference sounds by hash
  and path.

## Missing samples (§10.4)

- **Reported, never substituted.** A missing layer keeps its identity (hash, name, path). It
  is shown as missing in the layer list, on the waveform and on the pad, and is written back
  unchanged.
- **Find missing samples...** searches a folder and everything below it (moved libraries,
  offline drives brought back):
  - first files with the same name, then any audio file (renamed files);
  - a file is used only when its SHA-256 content hash matches;
  - a different file with the same name is never used (tested).

  The result is reported, e.g. "Found 1 of 2 missing samples."
- **Relink...** on a layer (right-click in the layer list) points it at a file the user
  chooses. This is explicit, so it is allowed, and it is one undo step. For a layer that
  plays, the same item reads **Replace sample...**.

## Tests (`andor_drums_plugin_tests "[kits]"`, 4 cases)

- Duplicate: the layers are shared, the settings and name copied, the note kept, and undo
  restores the target.
- Kits:
  - save, then New (undo restores);
  - open a kit bit-identically, with undo;
  - an OSP file is refused;
  - a newer schema is refused.
- Portable: duplicate file names are kept apart, no private paths are written, and the kit
  opens on "another computer" bit-identically.
- Missing samples:
  - a renamed file is found by content, and the same-named impostor is ignored;
  - manual relink is undoable.
