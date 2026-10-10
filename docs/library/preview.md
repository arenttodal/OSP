# Preview and audition (Stage 4 design)

Hearing a sound before it changes the instrument.

## Preview engine

- Pure C++ (`src/library/preview`), real-time safe in `render()`: no allocation, no locks, no
  file access. Buffers are decoded and resampled to the host rate on the Library worker, then
  handed over like the instrument's `ModelExchange`: an atomic pointer swap; the old buffer is
  released on the message thread once the audio thread has stopped using it.
- Three audition slots (A, B, C) and a single "preview" slot share the engine. Each slot holds
  one prepared buffer.
- Playing: `play(slot)` (one-shot from the start, dry), `stop()`, `restart()`; `noteOn(note)`
  plays the selected slots pitched against their root (bounded: 8 preview voices, oldest
  stolen with a 5 ms fade), so a combination can be auditioned on the keyboard.
- Gain: one preview gain (default -6 dB, saved per user, not per patch), and an equal-power sum
  of the slots that play together, so a combination is not louder than a single sound.
- Output: added to the plugin's output after the instrument, so it is heard through the same
  host channel, but never through the instrument's effects (dry by default, spec 4.6).
- The instrument is never changed by previewing: no parameter, no layer, no undo step.

## Commit ("Load combination")

1. Resolve each slot's sound (store, then places seen, by bytes).
2. The patch as it was is remembered (`rememberPreviousState`), so the load is recoverable.
3. Each slot goes to its layer through the existing loader (`loadIntoLayer`), in one undo
   step per layer as today; source settings stay as they are (the loader keeps them).
4. Each load is recorded in history with its layer.

## Load actions

Load into A / B / C: replaces that layer's sound (recoverable). Load into first empty: the
first empty layer; if none is empty the browser asks which to replace (it never takes A
silently).
