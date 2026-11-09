"""Curated selection: keep only the best extracted notes, spread them out, stretch them across the
keyboard.

The aim is not a faithful multi-sample of what was played but *some* playable instrument derived
from the recording: a handful of notes that came out well, each covering a zone. Every extracted
note is scored from its own audio and its extraction record (all measured, no listening):

    ownership    the share of the energy under its partials its mask was given (P2): low when
                 the note was taken out of a dense chord or a full mix
    tuning       how close it sits to a key: a note 40-50 cents off is usually a misreading
                 (organ vibrato, a beating unison, a partial of something else)
    stability    the spread of its pitch over its first 0.6 s (cents): a note whose pitch wanders
                 is a poor one to transpose
    harmonicity  the share of the sample's energy that lies on its own partials: drums, noise and
                 leftovers of other notes lower it
    length       how long it rings before it has died away
    level        its peak against the loudest extracted note

Hard gates drop the clearly unusable (off tuning, too short, too quiet, not harmonic, or scoring
under 35 % of the best note). The survivors are taken
best first, each only if no kept note is within `spacing` semitones, up to `max_notes`. If the
gates leave nothing, the best few by score are kept anyway: the result always plays.
Dropped notes stay in the atlas, excluded, with the reason, so OSP's NOTE MAP can bring them back.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ..audio import mono, read_audio
from .prepare import PreparedNote

KEYBOARD = (21, 108)          # the piano range every key of which should sound


@dataclass
class CurateConfig:
    spacing: int = 3              # semitones between kept notes (at least)
    max_notes: int = 12
    max_cents: float = 25.0
    min_seconds: float = 0.25
    max_below_loudest_db: float = 30.0
    min_harmonicity: float = 0.35
    fallback_notes: int = 3
    min_relative_score: float = 0.35   # a kept note scores at least this share of the best one


def measure(note: PreparedNote, root: Path, inharmonicity: float) -> dict:
    """Stability, harmonicity and ringing length of one extracted note, from its WAV."""
    x, sr = read_audio(root / note.sample_path)
    x = mono(x)
    onset = note.attack_start
    seg = x[onset:onset + int(0.6 * sr)]
    f0 = note.pitch_hz
    out = {"stabilityCents": float("nan"), "harmonicity": 0.0,
           "ringSeconds": max(0.0, (len(x) - onset) / sr)}
    if len(seg) < 2048 or f0 <= 0 or np.max(np.abs(seg)) <= 0:
        return out
    n_fft = 8192 if f0 < 150 else 4096
    hop = n_fft // 4
    win = np.hanning(n_fft)
    pad = np.concatenate([seg, np.zeros(max(0, n_fft - len(seg)))])
    frames = [pad[i:i + n_fft] * win for i in range(0, len(pad) - n_fft + 1, hop)] or [pad[:n_fft] * win]
    spec = np.abs(np.fft.rfft(np.array(frames), 4 * n_fft, axis=1))
    bin_hz = sr / (4 * n_fft)
    # stability: per frame, the interpolated peak of the strongest of partials 1-4, in cents
    track = []
    for S in spec:
        best = None
        for k in range(1, 5):
            hz = k * f0 * np.sqrt(1 + inharmonicity * k * k)
            lo, hi = int(hz * 2 ** (-60 / 1200) / bin_hz), int(hz * 2 ** (60 / 1200) / bin_hz) + 1
            if hi >= len(S) - 1 or lo < 1:
                continue
            i = lo + int(np.argmax(S[lo:hi]))
            if best is None or S[i] > best[0]:
                best = (S[i], i, k)
        if best is not None and best[0] > 0:
            _, i, k = best
            y0, y1, y2 = np.log(S[i - 1:i + 2] + 1e-20)
            d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) < 0 else 0.0
            track.append(1200 * np.log2((i + d) * bin_hz / (k * f0 * np.sqrt(1 + inharmonicity * k * k))))
    if len(track) >= 2:
        out["stabilityCents"] = float(np.std(track))
    # harmonicity: energy within +-25 cents (at least 2 bins) of the partials, against all of it
    P = np.mean(spec ** 2, axis=0)
    f = np.arange(len(P)) * bin_hz
    on = np.zeros(len(P), bool)
    k = 1
    while True:
        hz = k * f0 * np.sqrt(1 + inharmonicity * k * k)
        if hz > min(12000.0, 0.45 * sr) or k > 60:
            break
        w = max(2 * bin_hz, hz * (2 ** (25 / 1200) - 1))
        on |= np.abs(f - hz) <= w
        k += 1
    band = (f >= 0.5 * f0) & (f <= min(12000.0, 0.45 * sr))
    tot = float(np.sum(P[band]))
    out["harmonicity"] = float(np.sum(P[band & on]) / tot) if tot > 0 else 0.0
    return out


def curate(notes: list[PreparedNote], extraction: dict, cfg: CurateConfig | None = None) -> list[PreparedNote]:
    cfg = cfg or CurateConfig()
    root = Path(extraction["_root"])
    by_index = {int(n["index"]): n for n in extraction["notes"]}
    loudest = max((n.peak_db for n in notes), default=0.0)
    for n in notes:
        q = measure(n, root, float(by_index[n.index].get("inharmonicity", 0.0)))
        n.extra.update(q)
        own = float(n.extra.get("ownership", 1.0))
        tuning = float(np.clip(1.0 - abs(n.cents) / 50.0, 0.0, 1.0))
        stab = q["stabilityCents"]
        stability = 0.5 if not np.isfinite(stab) else float(np.clip(1.0 - stab / 30.0, 0.0, 1.0))
        length = float(np.clip(q["ringSeconds"] / 1.5, 0.1, 1.0))
        level = float(np.clip(1.0 - (loudest - n.peak_db) / cfg.max_below_loudest_db, 0.05, 1.0))
        harm = float(np.clip(q["harmonicity"], 0.0, 1.0))
        n.score = float(own ** 0.5 * tuning * stability ** 0.5 * harm * length ** 0.5 * level ** 0.5)
        n.extra["quality"] = {"ownership": round(own, 3), "tuning": round(tuning, 3), "stability": round(stability, 3),
                              "harmonicity": round(harm, 3), "length": round(length, 3), "level": round(level, 3)}
        n.included, n.excluded_because = True, ""
        if abs(n.cents) > cfg.max_cents:
            n.included, n.excluded_because = False, f"{n.cents:+.0f} cents off its key"
        elif q["ringSeconds"] < cfg.min_seconds:
            n.included, n.excluded_because = False, f"too short ({q['ringSeconds']:.2f} s)"
        elif loudest - n.peak_db > cfg.max_below_loudest_db:
            n.included, n.excluded_because = False, f"{loudest - n.peak_db:.0f} dB under the loudest note"
        elif harm < cfg.min_harmonicity:
            n.included, n.excluded_because = False, f"mostly not its own partials (harmonicity {harm:.2f})"
    best = max((n.score for n in notes if n.included), default=0.0)
    for n in notes:
        if n.included and n.score < cfg.min_relative_score * best:
            n.included, n.excluded_because = False, f"weak ({n.score:.2f} against the best {best:.2f})"
    candidates = [n for n in notes if n.included]
    if not candidates:
        candidates = sorted(notes, key=lambda n: -n.score)[:cfg.fallback_notes]
        for n in candidates:
            n.excluded_because = "kept anyway (nothing passed the checks)"
    kept: list[PreparedNote] = []
    for n in sorted(candidates, key=lambda n: (-n.score, n.index)):
        if len(kept) >= cfg.max_notes:
            break
        near = next((k for k in kept if abs(k.root_midi - n.root_midi) < cfg.spacing), None)
        if near is None:
            kept.append(n)
    kept_ids = {id(n) for n in kept}
    for n in notes:
        if id(n) in kept_ids:
            n.included = True
            if not n.excluded_because.startswith("kept anyway"):
                n.excluded_because = ""
        elif n.included or n.excluded_because.startswith("kept anyway"):
            near = min(kept, key=lambda k: abs(k.root_midi - n.root_midi))
            n.included = False
            n.excluded_because = (f"{near.name} ({near.id}) scores higher within {cfg.spacing} semitones"
                                  if abs(near.root_midi - n.root_midi) < cfg.spacing else f"beyond the {cfg.max_notes} best")
    return notes


def stretch_to_cover(keys: list[int], keyboard: tuple[int, int] = KEYBOARD) -> float:
    """The FILL reach that leaves no key of `keyboard` silent."""
    if not keys:
        return 0.0
    return float(max(min(abs(k - c) for c in keys) for k in range(keyboard[0], keyboard[1] + 1)))
