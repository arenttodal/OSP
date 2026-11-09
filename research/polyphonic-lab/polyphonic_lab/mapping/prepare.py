"""P3 sample preparation and candidate selection: extracted notes -> playable sample regions.

Each extracted note is already its own WAV (P2 `notes/`): a 20 ms pre-roll, the attack, the body
while the key is held, the damped tail, then silence. Here every note gets the regions OSP's Note
Atlas plays from (in that file's frames):

    sourceStart = 0
    attackStart = onset - 5 ms           where playback starts (keeps the hammer's edge; detected
                                         onsets are a few ms late or early)
    sustain     = the last 0.5 s before the key release (ending 50 ms before it), and never
                                         before onset + 100 ms. A piano has no steady part: OSP's
                                         continuation plays from this region when a key is held
                                         past the recording, so it must sit where the level is
                                         already low, or the held note swells back up (measured:
                                         +8 dB with the region at the start of the body)
    releaseStart = key release           (the damper)
    sourceEnd   = the end of the file    (60 dB below the peak, faded)

and a confidence in [0, 1]: the detector's confidence times how much of the energy under its own
partials the note's mask was given (`ownership`, P2), mapped to 0.5..1. A note sharing every
partial with a louder one scores low; an isolated one scores near its detector confidence.

Selection. One recording per key: the candidate with the highest score (confidence x a length
term that prefers notes held at least a second x a level term) is included; the others stay in
the atlas, excluded, where OSP's NOTE MAP can switch them on (or `include="all"` keeps all of
them, which OSP plays as round-robin variations of the key).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..transcription.events import hz_to_midi

ATTACK_LEAD_S = 0.005
SUSTAIN_AFTER_ONSET_S = 0.10
SUSTAIN_BEFORE_RELEASE_S = 0.05
SUSTAIN_SECONDS = 0.5
MIN_PEAK_DBFS = -60.0


@dataclass
class PreparedNote:
    index: int                    # into the extraction's notes
    id: str
    name: str
    sample_path: str              # relative to the extraction folder
    sample_rate: int
    frames: int
    attack_start: int
    sustain: tuple[int, int]
    release_start: int
    pitch_hz: float
    midi_pitch: float
    root_midi: int
    cents: float
    confidence: float
    peak_db: float
    rms_db: float
    articulation: str
    included: bool = True
    excluded_because: str = ""
    score: float = 0.0
    extra: dict = field(default_factory=dict)


def prepare(extraction: dict) -> list[PreparedNote]:
    """extraction: an osp-poly.extraction/1 document (outputs.load_extraction)."""
    sr = int(extraction["sampleRate"])
    out = []
    for n in extraction["notes"]:
        s = n["sample"]
        frames = int(round((s["end_seconds"] - s["start_seconds"]) * sr))
        onset = int(round(s["attack_seconds"] * sr))
        release = int(round((n["offset_seconds"] - s["start_seconds"]) * sr))
        attack = max(0, onset - int(round(ATTACK_LEAD_S * sr)))
        release = int(np.clip(release, attack + 1, max(attack + 1, frames - 1)))
        earliest = min(release, onset + int(round(SUSTAIN_AFTER_ONSET_S * sr)))
        sus1 = max(earliest, release - int(round(SUSTAIN_BEFORE_RELEASE_S * sr)))
        sus0 = max(earliest, sus1 - int(round(SUSTAIN_SECONDS * sr)))
        f0 = float(n["f0_hz"])
        midi = hz_to_midi(f0)
        root = int(n["pitch_midi"])
        det = float(n.get("confidence", 1.0))
        own = float(n.get("ownership", 1.0))
        conf = float(np.clip(det * (0.5 + 0.5 * own), 0.0, 1.0))
        held = n["offset_seconds"] - n["onset_seconds"]
        out.append(PreparedNote(
            index=int(n["index"]), id=n["id"], name=n["name"], sample_path=s["path"], sample_rate=sr, frames=frames,
            attack_start=attack, sustain=(sus0, sus1), release_start=release, pitch_hz=f0, midi_pitch=midi,
            root_midi=root, cents=(midi - root) * 100.0, confidence=conf, peak_db=float(s["peak_dbfs"]),
            rms_db=float(s["rms_dbfs"]), articulation="sustained" if held >= 0.4 else "short",
            extra={"ownership": own, "detectorConfidence": det, "heldSeconds": held}))
    return out


def select(notes: list[PreparedNote], include: str = "best") -> list[PreparedNote]:
    """Marks one candidate per key as included ("best") or keeps every usable one ("all")."""
    if include not in ("best", "all"):
        raise ValueError("include must be 'best' or 'all'")
    for n in notes:
        held = n.extra.get("heldSeconds", 1.0)
        n.score = n.confidence * min(1.0, 0.4 + 0.6 * held) * float(np.clip((n.peak_db + 60.0) / 40.0, 0.05, 1.0))
        n.included = True
        n.excluded_because = ""
        if n.peak_db < MIN_PEAK_DBFS:
            n.included, n.excluded_because = False, f"too quiet ({n.peak_db:.0f} dBFS)"
        elif abs(n.cents) > 50:
            n.included, n.excluded_because = False, f"pitch {n.cents:+.0f} cents off its key"
    if include == "best":
        by_key: dict[int, list[PreparedNote]] = {}
        for n in notes:
            if n.included:
                by_key.setdefault(n.root_midi, []).append(n)
        for key, group in by_key.items():
            best = max(group, key=lambda n: (n.score, -n.index))
            for n in group:
                if n is not best:
                    n.included, n.excluded_because = False, f"another take of this key scores higher ({best.id})"
    return notes
