# Preview and audition (Stage 4)

Hearing a sound before it changes the instrument.

## Preview engine (`src/library/PreviewEngine`)

- Pure C++, real-time safe in `render()`, `noteOn()`, `noteOff()`, `allNotesOff()`: no
  allocation, no locks, no file access (a test counts allocations made while rendering:
  zero).
- Four slots: the audition tray's A, B, C and the browser's own preview. Each holds one
  immutable `PreviewSound`: decoded audio (at most two channels, at the file's own rate,
  the first 60 s of anything longer), a 512-bin waveform overview and the root the
  keyboard plays it against.
- Hand-over like the instrument's `ModelExchange`: every `setSound()` makes a new entry
  stamped with a publish generation and stores its pointer atomically. The audio thread
  reads the slots once per block and reports the oldest generation it can still touch (the
  slots it read, every voice, and everything published after the block began). Entries are
  freed on the message thread, by the processor's timer, only below that. An entry never
  returns to a slot, so the report is safe even when the same sound sits in two slots.
- Sample rate: voices step through the sound at `fileRate / hostRate × pitch` with 4-point
  Hermite interpolation. Nothing is resampled ahead of time. A 44.1 kHz sound at 96 kHz
  keeps its pitch and length (test).
- Playing: `play(mask)` plays the slots once from the start, together, replacing the
  previous play. `stop()` fades everything out in 20 ms. The keyboard plays the chosen slots
  pitched against their roots, with a 0.5 ms fade-in so attacks are kept. Note-off fades in
  80 ms. At most 8 voices: a ninth steals the oldest with a 5 ms fade.
- Root: the catalog's root when known. Otherwise a YIN estimate over the first 8 s (the
  analysis' `PitchAnalyzer`, moderate confidence or better). Otherwise C4, and the sound
  says the root is unknown. Noise and silence never get an invented root (test).
- Gain: one preview gain (default -6 dB). It is the user's (`<Library>/settings.json`,
  schemaVersion 1), never the patch's. Slots started together share an equal-power sum
  (1/√n: three identical sounds are 1.73×, not 3×). A peak limiter (instant attack, 150 ms
  release) keeps the preview itself under -1 dBFS.
- Output: added after the instrument and its effects, into the same output. It is heard
  through the same host channel, never through SPACE, ECHO or the rest (dry, spec 4.6).

## Audition (`apps/plugin/Source/Audition`)

The plugin side, on the message thread:

- `preview(assetId)` / `previewFile(file)`: the browser's slot. It plays as soon as it is
  ready; a newer request wins.
- `setSlot(0..2, assetId)`, `clearSlot`, `play(mask)`, `stop()`, `slot(i)` (name, ready,
  error, overview), `setGainDb`.
- Finding a sound: the catalog thread resolves it (`resolveSound`: the store, then the
  places it was seen, checked by its bytes). The preview's own low-priority thread decodes
  it and estimates its root. The result is handed back on the message thread (the
  processor's timer calls `deliver()`). A sound that cannot be found or read says why, and
  the slot shows it.
- Keyboard: `OspAudioProcessor::setPreviewKeys(on, mask)`. While it is on, note-on and
  note-off (on screen and MIDI) go to the preview and the instrument sees every other
  message. Turning it on sends all-notes-off to the instrument and the arpeggiator, so
  nothing hangs.
- The instrument is never changed by previewing: no parameter, no layer, no undo step, no
  previous-state snapshot. A test compares the whole saved state before and after.

## Loading

- `loadIntoLayer(assetId, layer)`: one sound into A, B or C.
- `commit()` ("Load combination"): every tray slot that holds a sound goes to its own layer
  (A to A, B to B, C to C). An empty tray slot leaves its layer as it is.

Both resolve every sound first. If one cannot be found, nothing changes and the message says
which and where it was last seen. Then the processor (`loadLibrarySounds`):

1. Remembers the patch as it was (`rememberPreviousState`). The preset menu's "Back to the
   patch before …" undoes the whole load.
2. Loads each sound through the instrument's own loader (`LoadRequest`), with the sound's
   name and original place. The card shows "Low Drone A2.wav", not the managed copy's hash
   name, and the catalog finds the sound's own record instead of making another.
3. An occupied layer keeps its controls (a new sound gets its own root). A new layer starts
   neutral and is made audible in the mix, as a dropped sound would be.
4. Each load is a user load: recorded in the sound's history with its layer, and in the
   undo history as today.

"Load into first empty" is the GUI's: the first free layer
(`OspAudioProcessor::firstFreeLayer`). When none is free, the GUI asks which layer to
replace. A is never replaced silently.

## Storage (`library::storageUsage`, `library::emptyTrash`)

Policy: every sound loaded into the instrument keeps a managed copy in the sample store.
That is what makes projects and presets recall when the original moves (unchanged since
Phase 1).

- **Usage**: what the store holds, split by what holds it:
  - Library: a sound record or a preset needs it.
  - Trash only: only sounds in the Library's trash hold it.
  - Untracked: no record, e.g. loaded before the Library existed. Kept: projects may use it.
- **Emptying the Library's trash**: trashed records are removed for good. A trashed
  preset's file, and a stored copy that nothing else holds (no other record, no preset, not
  playing in this instance), are moved to the system Trash, so even this stays
  recoverable. A file that cannot be moved stays, and its record stays with it.
- What the Library cannot know: a DAW project that is not open. Such a project recalls
  from the sound's original place when the copy is gone. The Library settings say so
  before emptying.
