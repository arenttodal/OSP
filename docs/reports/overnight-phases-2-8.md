# Overnight report: Phases 2–8

Everything below is on branch `claude/charming-lovelace-n25fte`. Every commit passed
`ctest` (unit, integration, regression, performance smoke and the headless plugin tests)
before it was pushed, with one exception:

- the MPE commit was pushed with a recall regression;
- CI flagged it, and the next commit fixed it;
- every other commit was also green in CI on macOS (Apple Silicon, including `auval`)
  and Linux, and on Windows from the moment that job was added.

One page holds all the listening tests: **https://claude.ai/artifact/SBtjwht3czzVXQbzpmeWUR**.
The open verdicts are listed in `docs/morning-checklist.md`.

## Phase 2 — pitch bake-off (closed)

- **Ratings.** One listener rated 21 of 25 groups. Wins outside the 0-semitone
  controls: A 9, B 9, C 1. C scored 1/1/1 every time it was rated away from the root.
- **Decision.**
  - A (resampling) stays the default, because ties go to the simpler engine.
  - C (formant compensation) is rejected.
  - B becomes **Pitch Character: Natural**: offline register anchors at ±12 and ±24
    semitones, with resampling within ±6 of the nearest anchor. It is built as model
    stage 3.
  - Details: `docs/reports/pitch-bakeoff-1.md`.

## Phase 3 — continuation

- **Analysis** (`analysis/continuation`):
  - finds the stable region;
  - finds up to 32 jump points, matched on spectrum, level and level slope, and F0 and
    F0 slope, so tremolo and vibrato stay in phase;
  - aligns each jump by cross-correlation and gives it a correlation-aware crossfade;
  - finds graft exits into the recording's own ending.
  - Decaying and struck sources stay one-shot. Corpus: 47 of 80 files sustain.
- **Engine.** Playback is a random walk over the jumps that never repeats immediately.
  An optional movement layer adds drift. Release grafting plays the natural ending when
  Release ≥ 0.2 s.
- **Measured.** The repetition score (0.7–0.9 for a single loop) falls to 0.1–0.2 with
  multi-loop. No seams above 14 dB were found. A 3-second vowel holds for 20 s within
  3 dB of its level.
- **Exit test.** Lab tab "Sustain" (60-second holds on organ, vocal, bowed, tremolo
  and synth).

## Phase 4 — performance (LIFE)

- **Model.** A correlated performance vector: three latent factors with
  Ornstein-Uhlenbeck memory (4 s) drive level, brightness, body, transient, micro-pitch
  plus settling, damping, start offset and stereo position. Fast repeats alternate the
  attack and drift less in pitch.
- **Calibration.** Spreads are calibrated on the corpus' real repeated takes. LIFE 50 %
  is about two-thirds of the real spread; 80 % is about all of it.
- **Exit test.** Lab tab "Repetition".

## Phase 5 — dynamics

- **What velocity drives.** Velocity, relative to the recording (velocity 100 = as
  recorded), drives tilt, attack bite, transient, body, a sharp pitch start on hard
  notes, a softer attack on soft notes, and damping.
- **Measured.** Harmonic sources gain 3.5–6 semitones of brightness from velocity 20 to
  127. Near-sine sources change mainly in the attack (8–16 dB attack/body).
- **Exit test.** Lab tab "Dynamics".

## Phase 6 — Original ↔ Reimagined, CHARACTER, MOTION, SPACE

- **Post stage** (smoothed, so moving a control never clicks):
  - CHARACTER moves the source's own body resonances and tilts the spectrum;
  - a sympathetic resonator bank, tuned to the source's partials and body, grows toward
    Reimagined;
  - SPACE adds width, decorrelation for narrow sources, and FDN ambience.
- **Per voice:**
  - Reimagined gives shorter, more granular continuation, plus saturation above 50 %;
  - MOTION drives drift and stereo movement, and does almost nothing on plucks.
- **Exit test.** Lab tab "Original ↔ Reimagined" (five points on the continuum).
- **Transient/body separation (§19).**
  - Offline HPSS (median filtering) separates each layer's onset into body and
    transient.
  - Far from the root, the transient plays at its own speed, lined up on its peak, and
    its transposed copy is removed.
  - A synthetic pick two octaves down stays 4.5 ms long; without this it lasts 16 ms.
  - Lab tab "Transients".

## Phase 7 — multi-sample

- **Inference** (`engine/SampleSetInference`):
  - pitch groups;
  - velocity layers from dynamics words in the file names, or else from loudness gaps;
  - round robins and alternate articulations;
  - a confidence for each decision.
- **Engine.** Picks the nearest pitch, the velocity layer (with dynamics relative to
  that layer) and a round-robin take that never repeats. Applies a register brightness
  model.
- **Multi-velocity learning (§35).** Real soft and hard takes teach how loudness,
  brightness and attack change per layer step. Velocities between layers then move
  towards the neighbouring layer, so crossing a layer boundary no longer jumps.
- **Plugin.** Accepts multi-file and folder drops. The Samples inspector edits role,
  layer and root, rebuilding without re-analysis. Sessions recall bit-identically.
- **Register ground truth** (spec §70): mean error against plain transposition
  - plucks: 6.2 → 4.6 semitones;
  - organ: 5.2 → 4.8 semitones (its files mix organ stops);
  - Tagel: 3.0 → 3.1 semitones (neutral).
- **Exit test.** Lab tab "Multi-sample".

## Phase 8 — polish

**Done:**

- starting states;
- presets and portable instruments (bit-identical after moving to an empty sample store);
- undo/redo for loads and root changes;
- MIDI: mod wheel, pressure, CC74, CC 20–25 mapped to the macros, and MPE;
- interface size and accessibility titles;
- a preset browser (☰ → Presets, with sub-folders and previous/next) and a three-step
  empty state;
- Windows VST3 built and tested in CI;
- polyphase interpolation tables for unity and upward reads: engine C now costs about
  two-thirds of the plain baseline;
- a plugin test across host sample rates (22.05–192 kHz) and block sizes (1–4096, also
  varying): bit-identical output, no clicks.

**Written but not run:** `scripts/package-macos.sh` (signing and notarisation need a
Developer ID).

**Not done:**

- factory example sounds for onboarding (they need legally owned recordings, see the
  questions);
- host certification (needs a Mac).

## CPU (48 kHz, 128-sample blocks)

| Engine | Machine | 16 held voices | 24 voices, dense retriggers |
|---|---|---:|---:|
| Baseline A | macOS 14 arm64 CI | — | 26 % |
| Engine C, before the fast path | macOS 14 arm64 CI | 17.1 % | 30.5 % |
| Engine C, with the unity fast path | cloud x86 VM (noisy) | 22 % (baseline on the same VM: 31 %) | 41 % |
| Engine C, with stretched tables too | cloud x86 VM (noisy) | 10 % (an octave up: 17 %) | 21 % (baseline on the same VM: 32 %) |

The spec's target is 16 voices below 25 % on Apple Silicon. It was met before either
optimisation; together they cut engine C's cost by about half again. Worst-case callbacks on shared
CI machines spike to several milliseconds, so they need measuring on a real Mac.

## Known limits

All of these are recorded in STATUS:

- **Pitch tracking.** Very short or strongly formant-shaped synthetic vowels can be
  tracked on a harmonic.
- **Sets.**
  - Pitch groups ignore timbre (mixed organ stops).
  - Sets have no anchors.
- **Transients.** Transient/body separation (§19) only helps sources with a broadband
  attack; most of the corpus plucks have little.
- **CPU.** Reads more than two octaves up still use the per-sample stretched kernel.
- **UI.** The editor is a working UI, not the designed instrument UI.
