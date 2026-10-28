# ANDOR Grain — per-key polyphonic granular hybrid

ANDOR Grain is a polyphonic granular synthesizer in which every key has a stable, related
but individual granular identity: **KEY DNA**. A keyboard feels like one family of sounds,
not 128 unrelated patches. Sources: a sample, a Note Atlas from ANDOR Phrase, or no sample at
all (the oscillator).

| | |
| --- | --- |
| Plug-in | `apps/grain` (`ANDOR_Grain`: VST3, AU on macOS, Standalone) |
| Engine | `src/andor/grain/GrainEngine`, `src/andor/grain/KeyDna` |
| Identity | ANDOR Grain · `Ospx` / `Agrn` · `com.osp.andorgrain` · state tag `ANDOR_GRAIN_STATE` |

## Engine

- **One shared source**: the recording is decoded once into a padded, read-only
  `PlaybackSource` shared by every voice and key (and, in a hybrid, with Phrase's model:
  tested). Nothing copies the audio per key.
- **Voices** exist only while notes sound: key, velocity, region, a snapshot of the key's DNA
  profile, a per-trigger offset, smoothers, amp and aux ADSR, two LFOs, a TPT low-pass, an
  oscillator, a seeded PRNG, its grain list. Quality sets the voice limit: ECO 8, STANDARD 12,
  HIGH 16 (provisional, see `benchmarks.md`). Stealing: the oldest released voice, then the
  oldest, with a 5 ms fade; spare slots hold fading voices.
- **Grain pool**: 512 preallocated grains with a free list; each grain has its position,
  increment (pitch × direction), length, age, window phase and stereo gains. No allocation
  when grains start or end.
- **Scheduler** per voice: interval = 1 / density, jittered by TIMING JITTER; each grain takes
  SIZE, POSITION ± SCATTER inside its region, PITCH + PITCH SCATTER, a pan within SPREAD, and
  plays reversed with probability REVERSE.
- **Windows**: Hann, Soft (Tukey, flat middle half), Percussive (fast rise, exponential fall,
  short end taper) — precomputed 1024-point tables, linear interpolation, always ending at 0.
- **Bounds**: the read extent (length × rate) always fits the region: too-long grains are
  shortened, starts are mapped over the feasible span, reversed grains start at their far end;
  reads stay inside the padded source. A marker test proves no audible read outside a 100 ms
  region at extreme pitch, scatter, reverse and size.
- **Stereo**: both channels are read at the same position (coherent) and panned with
  equal-power gains, so the mono sum keeps the signal (tested); SPREAD 0 = identical channels.
- **Level**: overlapping grains add in power; each grain is scaled by 1/√(density × size) so
  SIZE × DENSITY changes do not jump in loudness.

### Global grain budget (deterministic)

ECO 96, STANDARD 192, HIGH 384 grains for all voices together. A new grain is allowed only if
the total stays under the budget and the voice stays under its fair share
(budget ÷ sounding voices, and the 160-slot per-voice list). When denied, the voice simply
skips that grain (its effective density drops) and `deniedGrains()` counts it. Decisions
depend only on counts, so the policy is deterministic; one dense note cannot starve the
others (tested: a second note gets its share while the first is saturated).

### Oscillator

A band-limited periodic source (`src/andor/dsp/Wavetable`): additive tables per octave hold
only harmonics below 0.45 × the sample rate for every fundamental of that octave; reads blend
neighbouring octaves (no timbre steps on glides) and neighbouring shapes (SHAPE 0 sine,
1 triangle, 2 saw, 3 square, continuous). Measured: a saw at C8 has its would-be aliases
more than 60 dB under the fundamental. OSC ↔ GRAIN mixes the two at equal power before the
voice filter and envelope; sparse-grain patches stay playable on the oscillator. Imported
wavetables are not supported in V1.

### Modulation (8 slots)

Sources: velocity, note, mod wheel (CC 1), amp envelope, aux envelope, LFO 1, LFO 2 (sine,
triangle, saw, square, seeded sample & hold; per voice, restarted at note-on), macro 1, 2.
Destinations: size, density, position, scatter, pitch scatter, grain level, OSC mix, cutoff,
spread. Amounts −1..1 of the destination's normalised travel; evaluated per voice every 32
samples.

### Parameter precedence (spec 18)

For the DNA parameters, in their normalised domain (0..1 knob travel):

1. base value (the knob)
2. \+ ½ × KEY DNA offset (curve × CURVE AMOUNT + individuality × VARIATION), snapshot at note-on
3. \+ ½ × per-trigger offset (TRIGGER VARIATION × a seeded draw per note-on)
4. \+ live modulation
5. clamp to 0..1, convert to units (SIZE 5 ms–1 s log, DENSITY 1–200/s log, PITCH SCATTER 0–12 st square law, TONE 200 Hz–20 kHz log, the rest linear)
6. smoothing (20 ms one-pole, control rate)
7. DSP

## KEY DNA

Seven parameters vary across the keyboard: SIZE, DENSITY, POSITION, SCATTER, PITCH SCATTER,
TONE, SPREAD. Three things are kept strictly apart:

| Layer | Where | Changes when |
| --- | --- | --- |
| stable note identity | the drawn curves + the seeded per-key individuality (KeyDnaTable) | the curves, SEED, VARIATION, SMOOTH ↔ INDIVIDUAL, CURVE AMOUNT change |
| per-trigger variation | a seeded offset drawn at each note-on (TRIGGER VARIATION) | every note-on (event counter) |
| per-grain randomness | scatter, pitch scatter, spread, reverse, jitter in the scheduler | every grain |

- **Curves**: up to 16 points per parameter, linear between them, flat outside.
- **Individuality**: a smooth field (random values every octave, cosine-interpolated) blended
  with independent per-key values, renormalised so the spread never changes. Neighbour
  correlation falls monotonically from about 0.97 (Smooth) to 0 (Individual) — the control
  is predictable (tested at five settings).
- **Cache**: 128 × 7 offsets in fixed arrays; recomputing allocates nothing (about 1000 PRNG
  draws), so the audio thread recomputes when a seed or curve changes. Curves are session
  state (not automatable parameters); the editor publishes curve snapshots to the audio
  thread through `ModelExchange`.
- **Controls**: CURVE AMT, VARIATION, INDIVIDUAL (Smooth ↔ Individual), SEED with NEW SEED
  (regenerate the individuality), RANDOMISE CURVE (a seeded random curve for the selected
  parameter), RESET CURVE, ZOOM (C3–C6 or A0–C8).
- **Editor**: click adds a point, drag moves it, double-click removes it, shift-drag draws;
  lavender bars show each key's individuality, the gold line the curve, orange dots the
  sounding keys' effective values (DNA + trigger + modulation), and the header the hovered
  key's resulting value. The keys under the curve audition.

## Sources and the Note Atlas

- **Sample**: WAV/AIFF up to 600 s; the root comes from OSP's own analysis (first 10 s) into
  the ROOT parameter, which the user can change.
- **Note Atlas** (`.andoratlas`, from ANDOR Phrase): the recording is found by fingerprint in
  the shared ANDOR Lab store, next to the atlas (relative path), at its last known path, or by
  name; a file of another length or rate is refused; a missing one is reported and the
  instrument stays silent but safe. Every included note becomes a region (its `grainRegion`,
  else its sustain, else its whole play range for very short sustains) at its own pitch.
- **Source policy** (ATLAS SOURCE): MAPPED (the note on the played key, takes in turn; else
  the nearest recorded key within an octave; else the closest pitch), CLOSEST (the note whose
  pitch is nearest the key), FIXED NOTE (one chosen note for every key), WHOLE SOURCE (the
  whole recording as one texture at ROOT: pitch then follows POSITION by design).
- At note-on: choose the region → its recorded root → transposition = key − root → the key's
  DNA profile → start the voice → modulation → output.

## State and presets

Host state `ANDOR_GRAIN_STATE` (version 1): parameters, the DNA curves (`<DNA><CURVE
param="size"><P k v/>…`), the source reference and, for an atlas source, the atlas JSON
itself. Presets: `Documents/ANDOR Lab/Grain Presets/*.andorgrain`. With DETERMINISTIC on
(default), the same preset, source and MIDI render identically (bit-exact, tested across
host block sizes and after recall); the seeded sequences restart when the host transport
starts. With it off, the event counter keeps running, so repeated performances differ (still
derived from the stored seed, never from time or hardware randomness).

## Real-time safety

Immutable model and curve snapshots through `ModelExchange`, atomics for parameters, a
lock-free audition queue, fixed pools; `processBlock` is tested allocation-free under notes,
pedal, bend, auditions, modulation and a KEY DNA change.
