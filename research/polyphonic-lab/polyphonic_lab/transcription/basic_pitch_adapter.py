"""Method 3: Basic Pitch (Spotify, Apache-2.0) as an external baseline.

It runs in its own environment (research/polyphonic-lab/.venv-basic-pitch, or the interpreter
named by OSP_POLY_BASIC_PITCH_PYTHON) through a small runner script, and its notes come back in
the lab's shared representation. It is not a dependency of OSP or of the lab's core.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from pathlib import Path

from .events import NoteEvent

LAB = Path(__file__).resolve().parents[2]
RUNNER = Path(__file__).with_name("_basic_pitch_runner.py")


def interpreter() -> Path:
    env = os.environ.get("OSP_POLY_BASIC_PITCH_PYTHON")
    return Path(env) if env else LAB / ".venv-basic-pitch" / "bin" / "python"


def available() -> bool:
    return interpreter().exists()


def detect(input_wav: str | Path, timeout_s: float = 600.0) -> tuple[list[NoteEvent], dict]:
    py = interpreter()
    if not py.exists():
        raise RuntimeError(f"Basic Pitch is not installed (no {py}); see research/polyphonic-lab/README.md")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "bp.json"
        env = dict(os.environ, TF_CPP_MIN_LOG_LEVEL="3", CUDA_VISIBLE_DEVICES="")
        r = subprocess.run([str(py), str(RUNNER), str(input_wav), str(out)], capture_output=True, text=True, timeout=timeout_s, env=env)
        if r.returncode != 0 or not out.exists():
            raise RuntimeError(f"Basic Pitch failed ({r.returncode}): {r.stderr.strip()[-600:]}")
        doc = json.loads(out.read_text())
    events = []
    for i, n in enumerate(sorted(doc["notes"], key=lambda n: (n["onset"], n["pitch_midi"]))):
        events.append(NoteEvent(id=f"note-{i + 1:04d}", pitch_midi=n["pitch_midi"], onset_seconds=round(n["onset"], 4),
                                offset_seconds=round(n["offset"], 4), velocity_estimate=round(n["amplitude"], 4),
                                confidence=round(n["amplitude"], 4), detector="basic-pitch"))
    meta = {"detector": "basic-pitch", "version": doc["basicPitchVersion"], "model": doc["model"], "inferenceSeconds": doc["seconds"]}
    return events, meta
