"""Transcription metrics (mir_eval), with every tolerance stated in the result.

Note-level, onset only:  pitch within 50 cents, onset within 50 ms.
Note-level, with offsets: also the offset within max(50 ms, 20 % of the reference note's length),
                          against the reference key release (offset_seconds).
Frame-level (polyphony): 10 ms frames; precision / recall / accuracy of the active pitch set.
Octave errors:           detected notes unmatched but an octave (+-12) from an unmatched
                         reference note with an onset within 50 ms.
"""

from __future__ import annotations

import numpy as np
import mir_eval

from ..transcription.events import NoteEvent, midi_to_hz

ONSET_TOL = 0.05
PITCH_TOL_CENTS = 50.0
OFFSET_RATIO = 0.2
OFFSET_MIN = 0.05
FRAME_S = 0.01


def _arrays(events: list[NoteEvent]):
    if not events:
        return np.zeros((0, 2)), np.zeros(0)
    iv = np.array([[e.onset_seconds, max(e.offset_seconds, e.onset_seconds + 1e-3)] for e in events])
    hz = np.array([midi_to_hz(e.pitch_midi) for e in events])
    return iv, hz


def evaluate(reference: list[NoteEvent], estimated: list[NoteEvent]) -> dict:
    ref_iv, ref_hz = _arrays(reference)
    est_iv, est_hz = _arrays(estimated)
    out = {"tolerances": {"onsetSeconds": ONSET_TOL, "pitchCents": PITCH_TOL_CENTS, "offsetRatio": OFFSET_RATIO,
                          "offsetMinSeconds": OFFSET_MIN, "frameSeconds": FRAME_S},
           "reference": len(reference), "estimated": len(estimated)}
    if len(reference) == 0 or len(estimated) == 0:
        out.update({"precision": 0.0, "recall": 0.0, "f1": 0.0, "f1WithOffsets": 0.0, "missed": len(reference),
                    "falseNotes": len(estimated), "octaveErrors": 0, "onsetErrorMedianMs": None, "offsetErrorMedianMs": None,
                    "frame": {"precision": 0.0, "recall": 0.0, "accuracy": 0.0}})
        return out
    p, r, f, _ = mir_eval.transcription.precision_recall_f1_overlap(ref_iv, ref_hz, est_iv, est_hz, onset_tolerance=ONSET_TOL,
                                                                    pitch_tolerance=PITCH_TOL_CENTS, offset_ratio=None)
    _, _, f_off, _ = mir_eval.transcription.precision_recall_f1_overlap(ref_iv, ref_hz, est_iv, est_hz, onset_tolerance=ONSET_TOL,
                                                                        pitch_tolerance=PITCH_TOL_CENTS, offset_ratio=OFFSET_RATIO,
                                                                        offset_min_tolerance=OFFSET_MIN)
    matches = mir_eval.transcription.match_notes(ref_iv, ref_hz, est_iv, est_hz, onset_tolerance=ONSET_TOL,
                                                 pitch_tolerance=PITCH_TOL_CENTS, offset_ratio=None)
    on_err = [abs(est_iv[j, 0] - ref_iv[i, 0]) * 1000 for i, j in matches]
    off_err = [abs(est_iv[j, 1] - ref_iv[i, 1]) * 1000 for i, j in matches]
    matched_ref = {i for i, _ in matches}
    matched_est = {j for _, j in matches}
    octave = 0
    for j in range(len(est_iv)):
        if j in matched_est:
            continue
        for i in range(len(ref_iv)):
            if i in matched_ref:
                continue
            semis = 12 * np.log2(est_hz[j] / ref_hz[i])
            if abs(abs(semis) - 12) < 0.5 and abs(est_iv[j, 0] - ref_iv[i, 0]) <= ONSET_TOL:
                octave += 1
                break
    out.update({
        "precision": round(float(p), 4), "recall": round(float(r), 4), "f1": round(float(f), 4), "f1WithOffsets": round(float(f_off), 4),
        "missed": int(len(ref_iv) - len(matched_ref)), "falseNotes": int(len(est_iv) - len(matched_est)), "octaveErrors": int(octave),
        "onsetErrorMedianMs": round(float(np.median(on_err)), 2) if on_err else None,
        "offsetErrorMedianMs": round(float(np.median(off_err)), 2) if off_err else None,
        "frame": _frame_metrics(ref_iv, ref_hz, est_iv, est_hz),
    })
    return out


def _frame_metrics(ref_iv, ref_hz, est_iv, est_hz) -> dict:
    end = max(ref_iv[:, 1].max(), est_iv[:, 1].max() if len(est_iv) else 0.0) + FRAME_S
    t = np.arange(0.0, end, FRAME_S)

    def frames(iv, hz):
        return [np.array([hz[k] for k in range(len(iv)) if iv[k, 0] <= ti < iv[k, 1]]) for ti in t]

    m = mir_eval.multipitch.evaluate(t, frames(ref_iv, ref_hz), t, frames(est_iv, est_hz))
    return {"precision": round(float(m["Precision"]), 4), "recall": round(float(m["Recall"]), 4), "accuracy": round(float(m["Accuracy"]), 4)}
