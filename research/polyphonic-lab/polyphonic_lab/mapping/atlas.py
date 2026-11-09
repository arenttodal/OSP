"""P3: an extraction -> an OSP Note Atlas (.andoratlas, schema 2) that the real OSP engine plays.

The atlas is written beside its samples in one self-contained folder:

    <out>/<name>.andoratlas        the atlas OSP reads (io::readNoteAtlas)
    <out>/samples/<note>.wav       one source per extracted note (float32 WAV)
    <out>/<name>.osp-poly.json     the lab's sidecar: provenance, selection reasons, P2 numbers

Each source is identified as OSP identifies assets, by the SHA-256 of the file's bytes, and
found through `relativePath` (relative to the atlas file), so the folder can move. The atlas uses
only fields OSP already reads; nothing about it is specific to the lab.

Mappings
    original  every included note on its own key; FILL lets the keys between borrow the nearest
              recorded note (up to `max_transpose` semitones). A playable instrument.
    compact   every included note on consecutive white keys from C4, untransposed, FILL off: an
              audition layout (each extracted note exactly as cut, side by side).
"""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

from .. import __version__
from ..audio import content_id
from ..extraction.outputs import load_extraction
from .prepare import PreparedNote, prepare, select

ATLAS_FORMAT = "andor.noteAtlas"
ATLAS_SCHEMA = 2
SIDECAR_SCHEMA = "osp-poly.atlas-sidecar/1"
WHITE = (0, 2, 4, 5, 7, 9, 11)


def _white_keys_from(start: int, count: int) -> list[int]:
    keys, k = [], start
    while len(keys) < count:
        if k % 12 in WHITE:
            keys.append(k)
        k += 1
    return keys


def atlas_note(i: int, p: PreparedNote, method: str, assigned_key: int = -1, variation: int = 0) -> dict:
    return {
        "id": f"n{i:04d}", "source": f"src{i}",
        "sourceStart": 0, "sourceEnd": p.frames, "attackStart": p.attack_start,
        "sustain": {"start": p.sustain[0], "end": p.sustain[1]}, "releaseStart": p.release_start,
        "fadeIn": int(round(0.002 * p.sample_rate)), "fadeOut": int(round(0.01 * p.sample_rate)),
        "pitchHz": round(p.pitch_hz, 4), "midiPitch": round(p.midi_pitch, 4), "rootMidi": p.root_midi,
        "cents": round(p.cents, 3), "stabilityCents": 0.0, "vibratoCents": 0.0, "vibratoHz": 0.0,
        "voicing": 1.0, "confidence": round(p.confidence, 4), "peakDb": round(p.peak_db, 2), "rmsDb": round(p.rms_db, 2),
        "articulation": p.articulation, "origin": "analysis", "method": method, "methodVersion": 1,
        "corrections": {"pitch": False, "boundaries": False, "key": False, "group": False, "split": False,
                        "merged": False, "velocity": False},
        "included": p.included, "assignedKey": assigned_key, "velocityLayer": 0, "group": "", "variation": variation,
    }


def export_atlas(extraction_dir: Path, out: Path, name: str = "instrument", mapping: str = "original",
                 include: str = "best", fill: bool = True, max_transpose: float = 12.0) -> dict:
    if mapping not in ("original", "compact"):
        raise ValueError("mapping must be 'original' or 'compact'")
    ex = load_extraction(extraction_dir)
    root = Path(ex["_root"])
    notes = select(prepare(ex), include)
    if not any(n.included for n in notes):
        raise ValueError(f"{extraction_dir}: no usable note to put in an atlas")
    out = Path(out)
    (out / "samples").mkdir(parents=True, exist_ok=True)
    atlas_path = out / f"{name}.andoratlas"
    method = f"osp-poly/{ex['method']}"
    detector = (ex["notes"][0].get("detector") if ex["notes"] else "") or ""
    sources, anotes, sidecar_notes = [], [], []
    keys = {}
    if mapping == "compact":
        inc = sorted((n for n in notes if n.included), key=lambda n: (n.midi_pitch, n.index))
        keys = {id(n): k for n, k in zip(inc, _white_keys_from(60, len(inc)))}
    variation: dict[int, int] = {}
    for i, p in enumerate(notes):
        src = root / p.sample_path
        dst = out / "samples" / Path(p.sample_path).name
        shutil.copyfile(src, dst)
        sources.append({
            "id": f"src{i}", "contentHash": content_id(dst), "origin": "fileImport", "fileName": dst.name,
            "relativePath": f"samples/{dst.name}", "lastKnownPath": str(dst.resolve()), "managedCopy": "",
            "sampleRate": p.sample_rate, "numChannels": 1, "numFrames": p.frames,
        })
        v = 0
        if p.included and mapping == "original":
            v = variation.get(p.root_midi, 0)
            variation[p.root_midi] = v + 1
        anotes.append(atlas_note(i, p, method, keys.get(id(p), -1), v))
        sidecar_notes.append({"atlasNote": f"n{i:04d}", "extractedNote": p.id, "name": p.name, "included": p.included,
                              "excludedBecause": p.excluded_because, "score": round(p.score, 4), **p.extra})
    settings = {"extraction": ex.get("settings"), "method": ex["method"], "residual": ex.get("residualStrategy"),
                "mapping": mapping, "include": include}
    digest = hashlib.sha256(json.dumps(settings, sort_keys=True, default=str).encode()).hexdigest()[:16]
    atlas = {
        "format": ATLAS_FORMAT, "schemaVersion": ATLAS_SCHEMA,
        "atlasId": f"osp-poly-{name}-{digest[:8]}", "createdBy": f"osp-poly {__version__} (research lab)",
        "analysis": {"method": method, "version": 1, "a4Hz": 440.0, "tuningOffsetCents": 0.0, "settingsDigest": digest},
        "mapping": {"fill": bool(fill and mapping == "original"), "maxTransposeSemitones": float(max_transpose), "correctTuning": True},
        "sources": sources, "notes": anotes,
    }
    atlas_path.write_text(json.dumps(atlas, indent=1) + "\n")
    sidecar = {
        "schema": SIDECAR_SCHEMA, "atlas": atlas_path.name, "labVersion": __version__,
        "input": ex.get("input"), "events": ex.get("events"), "detector": detector, "extraction": str(root),
        "method": ex["method"], "residualStrategy": ex.get("residualStrategy"), "mapping": mapping, "include": include,
        "consistency": ex.get("consistency"), "metrics": ex.get("metrics"), "notes": sidecar_notes,
    }
    (out / f"{name}.osp-poly.json").write_text(json.dumps(sidecar, indent=1) + "\n")
    return {"atlas": str(atlas_path), "notes": len(anotes), "included": sum(1 for n in notes if n.included),
            "keys": sorted({(a["assignedKey"] if a["assignedKey"] >= 0 else a["rootMidi"]) for a in anotes if a["included"]})}
