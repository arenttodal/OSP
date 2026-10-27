# ANDOR Lab changelog

## 0.1.0 — internal development build (branch `andor-lab`, 2026-10)

First versions of ANDOR Phrase and ANDOR Grain and their shared Note Atlas. No installer.

### Shared
- Note Atlas schema 1 (`src/andor/atlas`, `src/andor/io/NoteAtlasJson`): sources by
  fingerprint, absolute frame positions, attack / sustain / release, pitch, cents, stability,
  vibrato, confidence, corrections, variations, grain regions; exact JSON round trip;
  validation, repair, resolving, relinking.
- DSP: TPT state-variable filter, smoother, LFO, band-limited mipmapped wavetables, shared
  SPACE / ECHO (OSP's reverb and delay), sample-accurate offline render.
- Plug-in support: managed ANDOR Lab sample store, lock-free display mailboxes, lock-free
  audition queue, widgets on OSP's design system (`apps/andor-common`).
- research-renderer: `--phrase-analyze`, `--phrase-eval`, `--phrase-eval-file`,
  `--phrase-fixtures`, `--phrase-render`, `--phrase-ab`, `--andor-bench`.
- One accessor added to OSP's `YinPitchTracker` (read-only; OSP output unchanged).

### ANDOR Phrase
- Analysis: preprocessing with noise-floor awareness, YIN and YIN + Viterbi backends,
  vibrato- and tremolo-aware segmentation, robust pitch, boundary refinement, tuning rule,
  cache by content + version + settings.
- Engine: exact mapping, variations (in turn / random / shuffle), Fill Keyboard with explicit
  limits, ONE SHOT / GATE / SUSTAIN (correlated loops, release cross-fade, granular bloom and
  hold), tuning modes, attack preservation, Natural ↔ Synthetic, tone, level match, 4–28
  voices with stealing.
- Editing: markers, pitch / octave, PLAYS ON, include, split, merge, re-analyse note,
  re-analyse all keeping corrections, undo / redo.
- Plug-in `Ospx/Aphr`: background import with progress / cancel, drag and drop, editor,
  presets, host state with bit-identical recall, missing source + relink, atlas export.

### ANDOR Grain
- Engine: shared source, 512-grain pool, deterministic fair-share budget (96 / 192 / 384),
  ECO / STANDARD / HIGH voices (8 / 12 / 16), three windows, bounded reads, coherent stereo,
  oscillator blend, per-voice filter, amp / aux envelopes, two LFOs, 8 modulation slots,
  documented precedence chain.
- KEY DNA: 7 parameters with curves, seeded individuality with predictable correlation,
  separate per-trigger and per-grain randomness, cached table recomputed without allocation.
- Note Atlas sources with four policies; plug-in `Ospx/Agrn`: editor with the KEY DNA curve
  editor and live grain cloud, presets, bit-identical recall in deterministic mode.
