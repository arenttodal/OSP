# ANDOR Drums M3: sample layers (round robin, velocity, stack)

Implements spec Part 3 (§3.2 multiple samples on one pad, §3.3 round-robin modes, §3.5 the
layer editor). A pad holds 1 to 16 sample layers. A screenshot is in
`screens/13-layers-velocity.png`.

## What a musician does

- **Drop files on a pad.** No menu interrupts a drop: one file replaces the pad's sound,
  and several become round robins. **Add Layers...** adds round robins.
- **Hold Cmd (Ctrl on Windows) while dropping** to choose instead. A menu offers:
  - round robin: a different layer every hit;
  - velocity layers: soft to hard;
  - a stack: all together;
  - replace;
  - slicing a long file as a break;
  - sorting several files into the kit.

  The default is ticked.
- **Velocity layers** are sorted softest first, by the level each file was recorded at. Each
  gets an equal share of 1–127. OSP's loader levels every file, so the import restores each
  layer's recorded level against the loudest file, and a soft hit stays soft.
- **Modes:** ROBIN, VELOCITY or STACK on the pad.
  - Switching to VELOCITY spreads the ranges, unless they were already set.
  - Switching away gives every layer the full range again.
- **Order** (round robin, and velocity layers that overlap):
  - SHUFFLE (the default) plays every layer once before any repeats, and never the same one
    twice across bags;
  - SEQUENTIAL;
  - RANDOM never repeats the last one.
- **Layer list:**
  - hold ▶ to audition one layer;
  - click to select it, and the waveform shows that layer;
  - drag to reorder;
  - double-click to rename;
  - right-click to rename, duplicate, move up or down, or remove;
  - drop files on the list to add layers.

  A lit lens marks the layer the last hit played.
- **The selected layer's controls:**
  - LAYER LEVEL (−60 to +12 dB);
  - a VELOCITY range bar (drag either end, or the middle);
  - START and END handles on the waveform.

  With one layer, the waveform's START is the pad's START parameter, as before. END is
  always the layer's own.
- Every edit is one undo step: adding, mode, order, level, range, START/END, rename,
  reorder, duplicate, remove and clear. A drag is one step. Nothing is destructive: the
  recordings are never edited.

## How it plays (engine)

- `drums::LayerPlan` / `LayerPicker` (`src/drums/DrumLayers.*`, pure C++) choose the layers
  for a hit. The choice is reproducible from the kit's seed and the pad's hit count
  (CLAUDE.md rule 2): the same seed gives the same layers, and a bounce repeats exactly.
- `DrumKitEngine::setPadLayers` plays each chosen layer as one voice of the pad's OSP
  engine, through `InstrumentEngine::NoteOverride`. This is a one-shot, per-voice override
  of the next note-on on layer A. It sets:
  - the model;
  - the level;
  - START / END;
  - a pitch correction, so every layer sounds at its own natural pitch on the pad's key and
    EXPANDED transposes all of them alike.

  Hits of different layers ring out side by side.
- END is `InstrumentVoiceStart::endFraction`. The voice fades so that it is silent at END,
  checked at control rate with a fade of at least 1.5 ms.
- Both engine additions are inert unless set. OSP never sets them. OSP's unit, integration,
  regression (goldens) and performance suites pass unchanged. A pad with one layer at its
  defaults takes the old path, and its output is identical (tested).
- Layer buffers are not preallocated. A layer costs only its loaded sound.

## Threading and state

- Message to audio thread: every layer edit publishes one immutable `PadSound` bundle per pad
  through the existing `ModelExchange`. The bundle holds the sounds (shared) and the plan.
- On the audio thread:
  - a new bundle is applied at the block start;
  - an older bundle is freed once none of its own sounds is still sounding;
  - auditions of one layer go through the lock-free audition FIFO.

  Nothing on the audio thread allocates or locks.
- Multi-file drops load as one batch on the loader thread. When all files are back, they
  join the pad as one undoable edit. Files that fail are skipped, and if none loads the pad
  shows why.
- **State schemaVersion 3:** `<Pad><Layers mode order><Layer …/></Layers>`. Each layer has:
  - its sound identity and exact playback model (as `<Sound>`);
  - its name, level, start, end and velocity range.

  The first loaded layer is also written as `<Sound>`, so older builds still open the kit
  with one sample per pad.
- **Recall:**
  - sessions with state 1 or 2 open as one layer per pad (tested);
  - a missing layer keeps its identity and is written back unchanged;
  - a recalled kit is bit-identical (tested).

## Tests

- `osp_drums_tests "[layers]"` (9 cases):
  - the picker's orders: sequential, random without repeats, shuffle bags;
  - velocity ranges, and the nearest range when none holds the hit;
  - stack;
  - natural pitch per layer, also in EXPANDED;
  - one default layer is identical to the plain path;
  - level, END, and determinism by seed.
- `andor_drums_plugin_tests "[layers]"` (6 cases):
  - round robins in one undo step;
  - velocity layers sorted, with their recorded levels;
  - every edit undoable;
  - session recall (state 3, and state 2 as one layer);
  - auditioning one layer alone;
  - the editor: drops on a pad, list selection, the velocity bar as one undo step.

## Not done (later milestones)

- Humanisation controls per pad (§3.4: gain, pitch, start and tone variation). LIFE already
  varies every hit. Per-pad ranges come with the per-pad macros.
- Layer tuning and per-layer reverse. The pad's TUNE and REVERSE apply to all its layers.
- Smart grouping of a mixed drop into several pads (§6.4, Milestone 7).
