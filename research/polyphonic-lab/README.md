# Polyphonic Instrument Extraction Lab (research)

The pipeline: polyphonic audio → simultaneous note events → per-note audio → an OSP Note Atlas instrument
that the real OSP engine plays.

**Research only:**
- Not part of the plugin.
- Nothing here runs on the audio thread.
- The plugin is unchanged when the lab is not used.

Reports are in `docs/research/polyphonic-lab/`.

## Setup

```bash
cd research/polyphonic-lab
python3 -m venv .venv
.venv/bin/pip install -r requirements.lock
.venv/bin/pip install -e ".[test]"
# Optional: the Basic Pitch baseline (TensorFlow/ONNX). It is kept in its own environment.
python3 -m venv .venv-basic-pitch
.venv-basic-pitch/bin/pip install -r requirements-basic-pitch.lock
```

Generated audio, renders and runs go to `output/`, which is git-ignored. The corpus is regenerated
deterministically from code, so no audio is committed.

## Commands

```bash
.venv/bin/osp-poly corpus generate              # render every fixture (mixes + isolated stems) and its manifest
.venv/bin/osp-poly corpus validate              # hashes, formats, stems = mix; summary, spectrograms, evaluation manifest
.venv/bin/osp-poly detect <wav> --method harmonic|basic-pitch|ground-truth [--manifest m.json] --out <dir>
.venv/bin/osp-poly benchmark transcription [--sweep] --out <dir>
.venv/bin/osp-poly extract <wav> --notes notes.json --method A|B|C [--residual none|conservative|standard|generous] [--manifest m.json] --out <dir>
.venv/bin/osp-poly benchmark extraction [--sweep] [--events ground-truth harmonic] --out <dir>
.venv/bin/osp-poly export-atlas <extraction dir> --out <dir> [--name n] [--mapping original|compact] [--include best|all] [--render] [--plugin-check]
.venv/bin/osp-poly build <wav> --out <dir> [--detector harmonic|basic-pitch|ground-truth] [--manifest m.json] [--method C] [--render] [--plugin-check]
.venv/bin/pytest -q                             # the lab's tests
```

`--render` plays demo MIDI through OSP's own `research-renderer --atlas` (engine C);
`--plugin-check` loads the atlas into the OSP plugin processor headless, plays it, exports it as a
portable instrument and re-imports it (the hidden Catch2 test `[poly-lab-import]` in
`tests/plugin/PolyLabTests.inc`). Both need the repository built
(`cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin`); without it they
are skipped and say so. `OSP_RESEARCH_RENDERER` / `OSP_PLUGIN_TESTS` point at other builds.

## Layout

| Path | What |
|---|---|
| `polyphonic_lab/fixtures/` | the deterministic instrument synthesiser and the corpus definitions |
| `polyphonic_lab/corpus.py` | corpus generation, validation and the report |
| `polyphonic_lab/transcription/` | the shared note-event schema and the detectors |
| `polyphonic_lab/extraction/` | templates (partials, inharmonicity), masks (A, B), transients (C), the pipeline, outputs |
| `polyphonic_lab/mapping/` | sample regions and selection, Note Atlas export, OSP renders and the plugin check |
| `polyphonic_lab/evaluation/` | transcription and separation metrics, benchmarks, reports |
| `fixtures/manifests/` | committed fixture manifests (ground truth, hashes, provenance) |
| `configs/` | analysis configurations |
| `tests/` | pytest |
