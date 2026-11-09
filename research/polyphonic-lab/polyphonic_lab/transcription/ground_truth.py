"""Method 1: the fixture's known notes, so extraction can be judged without transcription errors."""

from __future__ import annotations

from pathlib import Path

from .. import corpus
from .events import NoteEvent


def detect(manifest_path: str | Path) -> tuple[list[NoteEvent], dict]:
    m = corpus.load_manifest(Path(manifest_path))
    return corpus.truth_events(m), {"detector": "ground-truth", "fixture": m["id"]}
