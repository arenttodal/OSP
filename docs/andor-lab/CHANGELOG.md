# ANDOR Lab changelog

## ANDOR Phrase 0.1.1 — first user tests (2026-10)

From the first real recordings (a ney and a cello melody; analysis version 2,
`phrase-analysis.md` "Round 3"):
- Breath and bow swells no longer cut a held note into fragments: re-articulation is measured
  against the median level either side (9 dB at NORMAL), and breathy voicing dropouts at the
  note's own level are bridged.
- A slide up into a phrase's first note becomes that note's attack (no more stray "E" on the
  key below that leaps up); falls, fading scraps and octave slips in a breathy tail become its
  release; octave slips are folded out of the pitch and vibrato measures.
- **DETECTION** (FEWER / NORMAL / MORE) and **MIN NOTE** (50–500 ms, default 250 ms) controls:
  saved with the session, not automatable; a change re-analyses in the background, keeps
  corrections and is one undo step that also restores the control (try, play, keep or undo).
- SUSTAIN loops are never shorter than 120 ms (shorter ones buzzed); notes too short for that
  hold with grains fitted inside the note, so its own pitch is heard.
- Breathy attacks after true silence are found from the silence, not from a noise-floor
  estimate made of the notes' own quiet moments.
- New stress fixtures `stress-breath-swells`, `stress-scoops`; CLI `--detection`, `--min-note`,
  `--output-contour`; `--phrase-fixtures` also writes the stress set; a hidden plug-in test
  renders the editor for any recording (`ANDOR_RECORDING`).
- The macOS test zip (`ANDOR-Lab-macOS-test`) has a double-click `install.command` (user-level
  copy of both plug-ins and apps, quarantine cleared), replacing the manual copy steps.
- Sessions saved with 0.1.0 reopen exactly as saved (no re-analysis); only a new import or a
  re-analysis uses version 2. Cached analyses of version 1 are not reused.

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
