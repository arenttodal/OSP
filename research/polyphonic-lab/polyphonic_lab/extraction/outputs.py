"""Writes an extraction to disk: playable note samples, full-length stems, residual, checks."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from ..audio import db, rms, sha256_file, write_audio
from ..transcription.events import note_name
from .separate import ExtractConfig, Extraction, trimmed

SCHEMA = "osp-poly.extraction/1"


def note_basename(i: int, ex: Extraction) -> str:
    return f"note-{i + 1:04d}_{note_name(ex.templates[i].event.pitch_midi)}"


def write_extraction(ex: Extraction, out: Path, cfg: ExtractConfig | None = None, meta: dict | None = None,
                     plots: bool = True) -> dict:
    cfg = cfg or ExtractConfig()
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    notes = []
    for i, t in enumerate(ex.templates):
        base = note_basename(i, ex)
        seg, start, end = trimmed(ex, i, cfg)
        sample = write_audio(out / "notes" / f"{base}.wav", seg[None, :], ex.sr)
        full = write_audio(out / "full" / f"{base}.wav", ex.notes[i][None, :], ex.sr)
        ev = t.event
        peak = float(np.max(np.abs(seg))) if len(seg) else 0.0
        notes.append({
            "id": ev.id or f"note-{i + 1:04d}", "index": i, "pitch_midi": ev.pitch_midi, "name": ev.name,
            "onset_seconds": round(ev.onset_seconds, 6), "offset_seconds": round(ev.offset_seconds, 6),
            "detector": ev.detector, "confidence": round(float(ev.confidence), 4),
            "f0_hz": round(t.f0, 4), "tuning_cents": round(t.cents, 3), "inharmonicity": float(f"{t.stiffness:.4g}"),
            "partials": int(len(t.partials_hz)), "measured_partials": int(np.count_nonzero(t.measured)),
            "ownership": round(float(ex.info.get("ownership", [0.0] * len(ex.templates))[i]), 4),
            "sample": {"path": str(sample.relative_to(out)), "sha256": sha256_file(sample),
                       "start_seconds": round(start, 6), "end_seconds": round(end, 6),
                       "attack_seconds": round(ev.onset_seconds - start, 6),
                       "peak_dbfs": round(db(peak), 2), "rms_dbfs": round(db(rms(seg)), 2)},
            "full": {"path": str(full.relative_to(out))},
        })
    write_audio(out / "residual.wav", ex.residual[None, :], ex.sr)
    write_audio(out / "reconstruction.wav", ex.reconstruction[None, :], ex.sr)
    write_audio(out / "reconstruction-error.wav", (ex.mixture - ex.reconstruction)[None, :], ex.sr)
    doc = {
        "schema": SCHEMA, "method": ex.method, "residualStrategy": ex.residual_strategy, "sampleRate": ex.sr,
        "settings": {"n_fft": cfg.n_fft, "hop": cfg.hop, "preroll_seconds": cfg.preroll_seconds,
                     "fade_in_seconds": cfg.fade_in_seconds, "fade_out_seconds": cfg.fade_out_seconds,
                     "end_floor_db": cfg.end_floor_db, "masks": dict(cfg.masks.__dict__, residual=ex.residual_strategy),
                     "templates": dict(cfg.templates.__dict__)},
        "seconds": round(ex.seconds, 3), "consistency": ex.consistency(), "info": _jsonable(ex.info),
        "residual": {"path": "residual.wav", "rms_dbfs": round(db(rms(ex.residual)), 2)},
        "reconstruction": {"path": "reconstruction.wav", "error": "reconstruction-error.wav"},
        "notes": notes, **(meta or {}),
    }
    if plots and ex.templates:
        from ..visualization.plots import mask_panels
        mask_panels(ex, out / "masks.png")
        doc["images"] = ["masks.png"]
    (out / "extraction.json").write_text(json.dumps(_jsonable(doc), indent=1) + "\n")
    return doc


def _jsonable(x):
    if isinstance(x, dict):
        return {k: _jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [_jsonable(v) for v in x]
    if isinstance(x, (np.floating, float)):
        v = float(x)
        return None if not np.isfinite(v) else round(v, 6)
    if isinstance(x, np.integer):
        return int(x)
    if isinstance(x, np.bool_):
        return bool(x)
    return x


def load_extraction(path: Path) -> dict:
    p = Path(path)
    if p.is_dir():
        p = p / "extraction.json"
    doc = json.loads(p.read_text())
    if doc.get("schema") != SCHEMA:
        raise ValueError(f"{p}: not an {SCHEMA} file")
    doc["_root"] = str(p.parent)
    return doc
