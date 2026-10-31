# SYNTHESIZE S1.1 — audit: PHRASE's Synthetic and where it fits in OSP

Brief: Phase S1 (an analog-style oscillator per voice, blended with the sample by one
SYNTHESIZE amount per source). Prototype read: `origin/andor-lab` at `eeb5f39`, extracted
read-only with `git archive` (never checked out).

## 1. What PHRASE did ("Natural / Synthetic")

| | PHRASE (`src/andor/phrase/PhraseEngine.cpp`, `renderVoice`) |
|---|---|
| Control | `synthetic` 0..1, default 0, host parameter "Natural / Synthetic" (one per instrument, not per source) |
| Oscillator | `andor::WavetableOscillator` over a shared `WavetableBank`: additive tables per octave (mipmaps, harmonics below 0.45 fs), shape position 2.0 = **saw**, one phase accumulator per voice, reset to phase 0 at note-on. `prepare()` allocates the bank; reads are real-time safe. |
| Pitch | `midiToHz (key + pitch + fine/100 + bend x range)`: the **played key**, not the region's recorded pitch; transposition corrections are not applied twice. |
| Level / envelope | **Sample-informed**: an envelope follower on the recording (`(|l| + |r|) / 2`, 5 ms attack, 60 ms release) scales the tone (`tone = osc x follower x 1.6`). The tone exists only while the recording sounds: on a pluck it decays with the pluck; when the region ends, it ends. The voice ADSR is applied after. |
| Blend | **Equal-power crossfade**: sample x cos(pi/2 a), tone x sin(pi/2 a). 0 = recording only; 50 % = both at -3 dB; 100 % = tone only (the recording only drives the follower). |
| Tone | The instrument's TONE SVF (300 Hz .. 20 kHz) after the mix, on both; no filter of the oscillator's own. |
| Character controls | None besides TONE. |

Known limitations (from the code):
- The follower ties the tone to the recording's loudness: no sustain beyond a pluck's decay,
  and noisy recordings (bow, breath) put their noise contour onto the tone.
- An uncalibrated equal-power law: the tone's loudness is the follower's mean-absolute level
  x 1.6, so a full saw lands about level with a sine-like recording and louder than a noisy
  one. Nothing measured.
- No tone of its own: a bright saw at the top of the range through an open TONE.
- One amount for the whole instrument.

## 2. What OSP has, and where the oscillator goes

- **Voice**: `InstrumentVoice` (one per sounding note per layer; allocation, stealing,
  pedal, mono/glide and MPE stay the engine's). In `render` (and `renderMode` for the
  REIMAGINED engines) the source is read first: the One Shot read (continuation crossfades,
  transient swap, release grains), or the granular cloud (`GranularSource`). Then the voice's
  shelves, CHARACTER, DAMPEN, envelope x level x velocity x fades, and pan.
  **The oscillator is mixed right after the source read and before those**, so everything
  the voice already does (CHARACTER filter, DAMPEN, ADSR, velocity, steal fade, pan, then the
  layer's EQ, mix and effects) applies to the hybrid exactly as to the sample.
- **Pitch**: the voice's step is `baseIncrement x pitchRatio x pitchMod x glide`, where
  - `baseIncrement` = the played key against the playing model's root (an Atlas member's own
    detected root);
  - `pitchRatio` = global bend and fine tune x the layer's TUNE x the layer's root correction
    (`rootRatio`, from a user root override) x MPE bend;
  - `pitchMod` = LIFE, settle, DRIFT and routed cents.
  The oscillator plays `midiToHz (played key) x step / baseIncrement / rootRatio`: everything
  intentional (bend, fine, TUNE, LIFE/drift/routes, glide), never the recording's own root or
  a root correction (they make the *sample* play the key; the oscillator already does). In
  Granular, the layer's granular TUNE is followed; grain SCATTER, KEY DNA's per-key offsets and
  LIFE's grain detune are not (they belong to the cloud).
- **Loudness reference**: `PlaybackPreparation` levels every source's loudest 20 ms to
  -16 dBFS RMS. The oscillator is calibrated against that, so 100 % lands near the level of
  the recording's loudest part, whatever the recording.

## 3. S1 design (and how it differs from PHRASE)

| | PHRASE | OSP S1 |
|---|---|---|
| Scope | one amount, whole instrument | **per source** (`layerX.synthesize`), A/B/C independent |
| Oscillator | mipmapped wavetable saw (bank allocated per engine) | **PolyBLEP saw** in the voice (no tables, nothing to allocate, exact at any rate); deterministic start phase from the note's seed (free-running feel in chords, same on every render) |
| Pitch | played key + bend/fine | played key + bend/fine/TUNE/LIFE/drift/routes/glide; no root correction |
| Envelope | follower on the recording | **A (default): gate** — about 12 ms attack, full sustain while the key (or pedal) holds, about 180 ms release; the voice's ADSR, velocity and steal fade on top. **B (internal)**: follows the recording's contour first, then settles into sustain over about 0.4 s. |
| Lifetime | ends with the region | a One Shot that runs out while the key is held keeps the voice for the oscillator (sample silent): a pluck can turn into a held tone. At 0 % voices end exactly as before. |
| Tone | instrument TONE SVF after the mix | a fixed 12 dB/oct low-pass on the oscillator only, around 6 x its pitch, opening a little with velocity (warm, not buzzy) |
| Blend | equal power | **hybrid** (chosen after comparing three laws; see `s1-results.md`) |
| At 0 % | — | not computed at all: renders bit-identical to before |

Not taken from PHRASE: its voice manager, loop, bloom, TONE, SharedFx.
