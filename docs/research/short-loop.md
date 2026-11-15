# LOOP's fallback for short notes

_15 Nov 2026. Branch `claude/osp-short-loop`._

## The problem

A layer's LOOP (One Shot, on by default) holds a note on the loops that the continuation analysis
finds in the recording. A short recording often has none:
- "too short" (under 0.2 s);
- "stable region too short" (the body under 0.5 s);
- "no body after the attack".

Such a note played once, however long the key was held, even with LOOP on. Note Atlas regions
never had this problem: since Phase 5 they use a *forced loop*, one crossfaded jump over the
note's body, aligned on its waveform (`ContinuationOptions::forceLoop`).

## The fix

- **Analysis** (`src/analysis/continuation`):
  - `wantsFallbackLoop (natural, analysis)`: true only when the natural analysis failed because
    the note is short (the three reasons above) and the note is not struck. Struck means an attack
    under 30 ms followed by a steady fall steeper than 10 dB/s. A struck or plucked note plays
    once, short or long, as a long pluck already did.
  - `fallbackLoop (audio, analysis, natural)` is Note Atlas's forced loop, with Note Atlas's 0.1 s
    shortest body (`fallbackLoopMinSeconds`).
  - Its result keeps the natural model's other fields, the PEDAL LOOP regions among them. When no
    loop fits it has `canSustain = false` and keeps the natural reason.
- **Model.** `PitchLayer::fallbackContinuation` sits beside `continuation`, so the natural model
  is never replaced.
  - It is built in `instrument::addContinuation`, in the background stage that already builds the
    continuation, so every loaded sound has it.
  - Register anchors re-align it, as they do the continuation.
  - It is in memory only (never persisted), so no cache or schema changes.
- **Engine.**
  - `LayerSettings::fallbackLoop` is **off by default**. The research renderer, baselines A/B
    and every golden render are therefore unchanged.
  - A voice uses the natural continuation when it can sustain. Otherwise, with LOOP on and the
    fallback allowed, it uses the fallback when there is one; failing both, the note plays once.
  - Granular, REVERSE and the release graft are unchanged: the forced loop has no graft exits.
  - Under the pedal, PEDAL LOOP Forward yields to the loop (as for any looping note); Ping-Pong
    still plays.
- **ANDOR OSP: the compatibility policy** (state version 16; your choice):
  - Every new session, and every sound the musician loads (drop, Browse, Library, examples,
    Record), has the fallback.
  - A session or preset saved before it (no `loopFallback` in its state) opens with the fallback
    **off for every layer that holds a saved sound**, so those sounds play exactly as before.
    Empty layers have it on.
  - The choice is saved per layer (`loopFallback`, e.g. "011"), so re-saving an old session keeps
    it as it was.
  - Loading a new sound into such a layer turns the fallback on for it.
  - It moves with the layer when layers are compacted.

## Duration matrix

The tests use sustained tones at 220 Hz with light vibrato and short fades, and a plucked string.

| Recording | Natural analysis | Fallback | Held 2 s with LOOP |
|---|---|---|---|
| sustained 0.1 s | too short | none (no body of 0.1 s) | plays once |
| sustained 0.3 s / 0.4 s / 0.6 s | no loop | one loop over the body | holds |
| sustained 3 s | its own loops | none needed | holds (unchanged, bit-identical) |
| pluck 0.4 s / 2 s | no loop / decaying source | none (struck) | plays once |
| silence | silent | none | silent |

## Tests

- `osp_tests "[short-loop]"`:
  - the matrix above;
  - a held 0.4 s note sustains with the fallback, plays once without it or with LOOP off;
  - a 3 s note is bit-identical either way.
- `osp_plugin_tests "[short-loop]"`:
  - a new session holds a short note;
  - the same session as saved by an older build (state 15) plays it once, and keeps doing so
    when re-saved;
  - its empty layers have the fallback;
  - loading a sound into the layer brings the fallback back.
- Hidden `[short-loop-renders]` (`OSP_SHORT_LOOP_RENDERS=<dir>`): before and after renders of
  the matrix, each held 3 s.

## Renders (measured)

Each note is held 3 s, A3 with SPACE 15 %. Levels are RMS:

| Recording | Before (0.05-0.2 s / 1.0-1.5 s / 2.5-3.0 s) | With the fallback |
|---|---|---|
| sustained 0.3 s | -32.4 / -69.0 / -97.3 dB | -32.4 / -33.8 / -33.9 dB |
| sustained 0.4 s | -32.6 / -69.0 / -97.0 dB | -32.6 / -32.5 / -33.0 dB |
| sustained 0.6 s | -32.6 / -66.1 / -94.5 dB | -32.6 / -32.6 / -33.2 dB |
| sustained 1.0 s (its own loops) | -32.5 / -32.7 / -33.2 dB | identical |
| pluck 0.4 s | -35.7 / -74.6 / -105.4 dB | identical (plays once) |

**The seam:**
- The largest sample-to-sample step while held is 0.0009-0.0010, the same as a natural loop's
  0.0010.
- The 10 ms level varies by 1.6-2.1 dB, against 2.0 dB for a natural loop.

There are no clicks, and the level stays as steady as a natural loop's.

## Not verified

- Listening on real short recordings (one-shot stabs, short vocal samples). The renders are
  synthetic.
- A fading short note's single loop can pulse a little, as Note Atlas's forced loops can.
