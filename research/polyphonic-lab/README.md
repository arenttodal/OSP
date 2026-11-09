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
.venv/bin/pytest -q                             # the lab's tests
```

Each later phase adds its commands to this list as it lands (`detect`, `extract`, `benchmark`,
`export-atlas`, `build`).

## Layout

| Path | What |
|---|---|
| `polyphonic_lab/fixtures/` | the deterministic instrument synthesiser and the corpus definitions |
| `polyphonic_lab/corpus.py` | corpus generation, validation and the report |
| `polyphonic_lab/transcription/` | the shared note-event schema and the detectors |
| `polyphonic_lab/extraction/` | spectral masks, reconstruction, transients, residual, quality |
| `polyphonic_lab/mapping/` | sample selection and Note Atlas export |
| `polyphonic_lab/evaluation/` | transcription and separation metrics, reports |
| `fixtures/manifests/` | committed fixture manifests (ground truth, hashes, provenance) |
| `configs/` | analysis configurations |
| `tests/` | pytest |
