# Polyphonic Instrument Extraction Lab — P0.0 audit

**Status:** research. Not an approved production feature.

| | |
|---|---|
| Starting commit | `c49d77f` (`claude/charming-lovelace-n25fte`, the default branch: S2 imprint, mode icons, Refined Core colours) |
| Research branch | `claude/polyphonic-lab` |
| Date | 9 Nov 2026 |
| Build machine | Linux x86-64 container, 4 cores, 15 GB RAM. No Apple Silicon here: performance figures are this machine's. |

## 1. What already exists, and what the lab reuses

| Concern | Where (current tree) | Used by the lab? |
|---|---|---|
| Note Atlas model (schema 2) | `src/model/NoteAtlas.{h,cpp}`: `SourceAsset` (content hash `sha256:<hex>`, paths only hints), `NoteEvent` (`sourceStart/End`, `attackStart`, `sustain`, `releaseStart`, fades, `pitchHz`, `midiPitch`, `rootMidi`, `cents`, `confidence`, `velocityLayer`, `variationGroup/Index`, `assignedKey`, `origin`, `analysisMethod`), `Mapping` (`fill`, `maxTransposeSemitones`, `correctTuning`, `speed`) | **Yes: the import contract.** The lab writes schema-2 atlases with the existing fields only. Research data goes to a sidecar. |
| Atlas JSON | `src/io/NoteAtlasJson.{h,cpp}`: `readNoteAtlas`, `writeNoteAtlas`; `io::resolveSource` finds audio by hash via `relativePath` / `lastKnownPath` / search folders; extension `.andoratlas` | **Yes:** the lab's exporter writes this exact format (checked against `research-renderer --write-atlas-fixtures` output). |
| Atlas → instrument | `src/engine/AtlasSet.{h,cpp}`: `buildPlayableSet` (Stage A: playable at once) and `buildInstrumentSet` (A then Stage B: continuation); one `InstrumentSet` member per note (region copy); FILL via `InstrumentSet::groupFor` | **Yes**, unchanged: every extracted note becomes one member through OSP's own builder. |
| Headless playback | `research-renderer --atlas <file.andoratlas> (--midi \| --fixture) --output <wav> [--mode granular] [--no-fill]` | **Yes:** demo renders go through the real engine (engine C). |
| Plugin | `OspAudioProcessor::loadAtlas (atlas, layer, atlasFolder)`, `exportInstrument` / `importInstrument` (`.ospinstrument`, which carries the atlas's recordings), sessions (state v14 `Atlas` child) | **Yes**, through a hidden, env-driven plugin test (no production code). It loads the lab's atlas, renders, exports a `.ospinstrument`, reopens it in a fresh processor with a fresh sample store, and checks it plays. |
| Phrase analysis (monophonic) | `src/analysis/phrase` (yin-viterbi, segmentation; MAP NOTES) | **No.** It is monophonic by design and stays unchanged. The lab is a separate operation ("extract polyphonic notes", not MAP NOTES). |
| Pitch (YIN), onsets, spectrum, spectral envelope, transient/body HPSS | `src/analysis/pitch`, `onset`, `spectrum`, `transient`; `core/Fft` | Not called. The research DSP is Python (spec §5). The C++ equivalents are the reference for a later port, if the experiment succeeds. |
| Continuation / loops | `src/analysis/continuation`, Stage B | Indirectly: extracted notes get loops from Stage B like any atlas note. |
| Undo, background model exchange | `ModelExchange`, the loader thread | Indirectly: a lab atlas loads through the same path as a MAP NOTES atlas. |
| Goldens, fixtures | `tests/regression/golden/*`, `src/midi/MidiFixtures` (frozen) | Not modified. The lab's fixtures are its own, generated under `research/polyphonic-lab`. |
| Python tooling | Scattered scripts (`research/listening/build.py`, `scripts/visual-compare.py`, design-lab); no package or pinned environment | The lab brings its own package, venv and lock (`research/polyphonic-lab`). |

## 2. The import route (audit gate)

```
polyphonic audio ──► osp-poly detect ──► notes.json ──► osp-poly extract ──► extracted/*.wav + metrics
                                                                                   │
                           osp-poly export-atlas: one source asset per extracted note,
                           sha256 of the WAV's bytes, relativePath beside the atlas,
                           one NoteEvent per note (whole file; attackStart after a short
                           pre-roll; sustain and release from the note's envelope)
                                                                                   ▼
                                                 instrument/<name>.andoratlas + <note>.wav
                                       ┌───────────────────────┴────────────────────────┐
                    research-renderer --atlas … (engine C)        osp_plugin_tests "[poly-lab-import]"
                    renders: notes, scale, new chord              (OSP_POLY_ATLAS / OSP_POLY_OUT):
                                                                  loadAtlas → play → exportInstrument →
                                                                  importInstrument (fresh store) → play
```

Before the extraction work, this route was checked with the renderer's own fixture atlases: `--write-atlas-fixtures` output read with Python matches the JSON the lab writes. A plugin regression test covers the same multi-source shape (three single-note WAVs, one source each): `[plugin][poly-lab]`.

**Gate: passed.** No production C++ changes are needed. The bridge is test code: a hidden, env-driven case plus one ordinary regression case.

## 3. Constraints recorded

- **Recordings:** none of the user's recordings are in this container. Hosts with freely licensed piano samples (University of Iowa MIS, FreePats) are blocked by this environment's network policy; PyPI is reachable.
  - P0–P3 therefore use a **deterministic synthesised piano**: inharmonic partials (stiffness coefficient B by register), unison detuning (beating), a hammer-position comb, two-stage frequency-dependent decay, a hammer-noise transient, damper release and sustain-pedal sympathetic resonance. Every mixture has exact isolated ground truth.
  - A synthetic piano is easier than a real one in some ways (exact harmonic model families, no room) and harder in others (it is honest to its model). **Fixture D (a genuine recorded performance) is pending** until recordings are supplied or a sample host is allowed.
- **Python and ML:** Basic Pitch is an optional, external baseline adapter (spec P1 Method 3), installed only in the research environment. The plugin never loads Python or a model (CLAUDE.md rule 5 and spec §3).
- **Determinism:** numpy `Generator(PCG64)` with fixed seeds per fixture and note. Outputs are byte-identical across runs on one machine (tested).
- **Licensing:** all fixtures are generated here (no third-party audio). Basic Pitch is Apache-2.0, including its bundled weights. No redistribution in a product is proposed.

## 4. Plan (P0–P3, then stop)

| Phase | Deliverable |
|---|---|
| P0 | Synthetic corpus: fixture classes A (single notes), B (chords), C (overlapping attacks), D-synthetic (phrase over a chord), E (pedal), F (organ, guitar, strings, vocal harmony; secondary). JSON manifests; `osp-poly corpus validate` (summary, spectrograms, timelines, statistics, evaluation manifest). |
| P1 | Shared note-event JSON. Detectors: ground truth; a deterministic harmonic detector (iterative harmonic salience with spectral subtraction, inharmonicity, octave penalties, per-pitch tracking, onset refinement); a Basic Pitch adapter. Evaluation with `mir_eval` (explicit tolerances). |
| P2 | Extraction A (simple harmonic masks), B (harmonic NMF with fixed partial positions, temporally constrained, Wiener masks plus a residual), C (B plus an attack-aware short-window transient allocation and a conservative residual). Mixture-phase iSTFT; the mixture equals the notes plus the residual by construction. Metrics: SI-SDR, projection SIR/SAR, pitch, transient, spectral distance, consistency. |
| P3 | Sample preparation, candidate selection, atlas export, the plugin bridge (load, play, `.ospinstrument`, reopen), renders through the engine. End-to-end: a C-major chord becomes C, E and G samples and an instrument OSP plays. **Stop for review.** |
