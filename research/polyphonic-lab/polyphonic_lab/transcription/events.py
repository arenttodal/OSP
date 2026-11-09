"""The shared note-event representation (every detector writes it, every stage reads it)."""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass, field
from pathlib import Path

SCHEMA = "osp-poly.notes/1"
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_to_hz(midi: float, a4: float = 440.0) -> float:
    return a4 * 2.0 ** ((midi - 69.0) / 12.0)


def hz_to_midi(hz: float, a4: float = 440.0) -> float:
    return 69.0 + 12.0 * math.log2(hz / a4)


def note_name(midi: int) -> str:
    return f"{NAMES[midi % 12]}{midi // 12 - 1}"


@dataclass
class NoteEvent:
    id: str
    pitch_midi: int
    onset_seconds: float
    offset_seconds: float
    pitch_hz: float = 0.0
    velocity_estimate: float = 0.0     # 0..1 (a detector's estimate, or the fixture's velocity)
    confidence: float = 1.0            # the detector's pitch/timing confidence, 0..1
    source_id: str = "mix"
    detector: str = "ground-truth"
    status: str = "candidate"          # candidate | accepted | rejected
    extra: dict = field(default_factory=dict)

    def __post_init__(self):
        if not self.pitch_hz:
            self.pitch_hz = midi_to_hz(self.pitch_midi)

    @property
    def name(self) -> str:
        return note_name(self.pitch_midi)

    @property
    def duration(self) -> float:
        return max(0.0, self.offset_seconds - self.onset_seconds)


def save_events(path: str | Path, events: list[NoteEvent], meta: dict | None = None) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    doc = {"schema": SCHEMA, "meta": meta or {}, "notes": [_round(asdict(e)) for e in events]}
    path.write_text(json.dumps(doc, indent=1) + "\n")
    return path


def load_events(path: str | Path) -> list[NoteEvent]:
    doc = json.loads(Path(path).read_text())
    if doc.get("schema") != SCHEMA:
        raise ValueError(f"{path}: not an {SCHEMA} file")
    return [NoteEvent(**n) for n in doc["notes"]]


def _round(d: dict) -> dict:
    out = {}
    for k, v in d.items():
        out[k] = round(v, 6) if isinstance(v, float) else v
    return out
