"""P2 separation metrics against the corpus's isolated stems.

All measured, none perceptual. No metric here says whether a note sounds good; that needs
listening, which is reported separately.

  * SI-SDR (scale-invariant SDR, Le Roux et al. 2019) per note, full length.
  * SIR / SAR by orthogonal projection, a simplified BSS Eval with zero lag (no 512-tap filters):
      s_target = projection of the estimate onto its own reference,
      e_interf = projection onto all references minus s_target,
      e_artif  = the rest.
    Labelled "projection SIR/SAR" in reports, not bss_eval.
  * Pitch error (cents): the extracted note's f0 against the reference stem's f0, both measured
    the same way (median over partials 1-8 of peak / (k sqrt(1 + B k^2))).
  * Transient preservation: the attack's energy (first 30 ms, above 2 kHz) of the estimate
    against the reference, in dB, and the correlation of their 1 ms envelopes over the first 60 ms.
  * Log-spectral distance (dB) between estimate and reference over the note's sounding region,
    on bins within 60 dB of the reference's peak (the estimate floored there too).
  * Ratios are capped at 100 dB (an exact estimate's is infinite). The improvement over the
    mixture is only counted where the mixture is not already the note (single notes).
  * Oracle and anchor: the ideal ratio mask from the true stems (the best any mask can do with the
    same STFT) and the unprocessed mixture (what doing nothing scores).
"""

from __future__ import annotations

import numpy as np
from scipy.signal import butter, sosfiltfilt

from ..stft import Stft
from ..transcription.events import NoteEvent


CAP_DB = 100.0   # a perfect estimate's ratio is unbounded; reports read ">= 100" as "exact"


def _ratio_db(num: float, den: float) -> float:
    return float(min(CAP_DB, 10 * np.log10(max(num, 1e-300) / max(den, 1e-300))))


def si_sdr(est: np.ndarray, ref: np.ndarray) -> float:
    rr = float(np.dot(ref, ref))
    if rr <= 0:
        return float("nan")
    a = float(np.dot(est, ref)) / rr
    t = a * ref
    e = est - t
    return _ratio_db(float(np.dot(t, t)), float(np.dot(e, e)))


def projection_metrics(estimates: list[np.ndarray], refs: list[np.ndarray]) -> list[dict]:
    R = np.stack(refs)
    G = R @ R.T
    Ginv = np.linalg.pinv(G)
    out = []
    for j, est in enumerate(estimates):
        r = refs[j]
        t = (np.dot(est, r) / max(np.dot(r, r), 1e-300)) * r
        c = Ginv @ (R @ est)
        p_all = c @ R
        e_i = p_all - t
        e_a = est - p_all
        tt, ii, aa = float(np.dot(t, t)), float(np.dot(e_i, e_i)), float(np.dot(e_a, e_a))
        out.append({"sdr": _ratio_db(tt, ii + aa), "sir": _ratio_db(tt, ii), "sar": _ratio_db(tt + ii, aa)})
    return out


def measured_f0(x: np.ndarray, sr: int, nominal_hz: float, start_s: float, seconds: float = 0.6) -> float:
    a = int((start_s + 0.05) * sr)
    seg = x[a:a + int(seconds * sr)]
    if len(seg) < 1024 or np.max(np.abs(seg)) <= 0:
        return float("nan")
    n = 1 << 19
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    bin_hz = sr / n
    ests = []
    for b in (0.0, 1e-4, 3e-4, 8e-4):
        e, w = [], []
        for k in range(1, 9):
            hz = k * nominal_hz * np.sqrt(1 + b * k * k)
            if hz > 0.45 * sr:
                break
            lo, hi = int(hz * 2 ** (-40 / 1200) / bin_hz), int(hz * 2 ** (40 / 1200) / bin_hz) + 1
            i = lo + int(np.argmax(spec[lo:hi]))
            if 0 < i < len(spec) - 1:
                y0, y1, y2 = np.log(spec[i - 1:i + 2] + 1e-20)
                d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) < 0 else 0.0
                e.append((i + d) * bin_hz / (k * np.sqrt(1 + b * k * k)))
                w.append(spec[i])
        if e:
            ests.append((float(np.sum(w)), float(np.median(e))))
    return max(ests)[1] if ests else float("nan")


def transient_metrics(est: np.ndarray, ref: np.ndarray, sr: int, onset_s: float) -> dict:
    a = max(0, int((onset_s - 0.005) * sr))
    sos = butter(4, 2000.0, btype="high", fs=sr, output="sos")
    seg_e, seg_r = est[a:a + int(0.065 * sr)], ref[a:a + int(0.065 * sr)]
    if len(seg_r) < 64:
        return {"attackEnergyErrorDb": float("nan"), "envelopeCorrelation": float("nan")}
    he, hr = sosfiltfilt(sos, seg_e), sosfiltfilt(sos, seg_r)
    n30 = int(0.035 * sr)
    ee, er = float(np.sum(he[:n30] ** 2)), float(np.sum(hr[:n30] ** 2))
    hop = max(1, int(0.001 * sr))
    m = len(seg_r) // hop
    env = lambda s: np.sqrt(np.mean(s[: m * hop].reshape(m, hop) ** 2, axis=1))
    ce, cr = env(seg_e), env(seg_r)
    corr = float(np.corrcoef(ce, cr)[0, 1]) if np.std(ce) > 0 and np.std(cr) > 0 else float("nan")
    return {"attackEnergyErrorDb": float(10 * np.log10(max(ee, 1e-300) / max(er, 1e-300))), "envelopeCorrelation": corr}


def log_spectral_distance(est: np.ndarray, ref: np.ndarray, sr: int, onset_s: float, end_s: float) -> float:
    st = Stft(4096, 1024, sr)
    E, R = np.abs(st.forward(est)), np.abs(st.forward(ref))
    t = st.times(len(ref))
    sel = (t >= onset_s) & (t <= end_s)
    if not np.any(sel):
        return float("nan")
    E, R = E[:, sel], R[:, sel]
    floor = np.max(R) * 1e-3              # -60 dB: below it neither spectrum counts as different
    keep = R > floor
    if not np.any(keep):
        return float("nan")
    d = 20 * np.log10(np.maximum(E[keep], floor)) - 20 * np.log10(R[keep])
    return float(np.sqrt(np.mean(d * d)))


def oracle_irm(mix: np.ndarray, refs: list[np.ndarray], sr: int, n_fft: int = 8192, hop: int = 512) -> list[np.ndarray]:
    st = Stft(n_fft, hop, sr)
    X = st.forward(mix)
    P = [np.abs(st.forward(r)) ** 2 for r in refs]
    tot = np.sum(P, axis=0) + 1e-20
    return [st.inverse(p / tot * X, len(mix)) for p in P]


def match_events(ref: list[NoteEvent], est: list[NoteEvent], onset_tol: float = 0.05) -> dict[int, int]:
    """ref index -> est index: same MIDI pitch, onsets within the tolerance, closest first."""
    pairs = sorted(((abs(r.onset_seconds - e.onset_seconds), i, j) for i, r in enumerate(ref) for j, e in enumerate(est)
                    if r.pitch_midi == e.pitch_midi and abs(r.onset_seconds - e.onset_seconds) <= onset_tol))
    used_r, used_e, out = set(), set(), {}
    for _, i, j in pairs:
        if i not in used_r and j not in used_e:
            out[i] = j
            used_r.add(i)
            used_e.add(j)
    return out


def _improvement(notes: list[dict]) -> float:
    d = [n["siSdr"] - n["mixtureSiSdr"] for n in notes if n["mixtureSiSdr"] < CAP_DB]
    return float(np.median(d)) if d else float("nan")


def evaluate_notes(mix: np.ndarray, sr: int, ref_events: list[NoteEvent], refs: list[np.ndarray],
                   est_events: list[NoteEvent], estimates: list[np.ndarray], residual: np.ndarray | None = None,
                   residual_truth: np.ndarray | None = None) -> dict:
    """Per-note metrics for the reference notes; a missed note scores nothing and is counted."""
    match = match_events(ref_events, est_events)
    proj_refs = refs + ([residual_truth] if residual_truth is not None else [])
    proj_est = [estimates[match[i]] if i in match else np.zeros_like(mix) for i in range(len(refs))]
    proj = projection_metrics(proj_est, proj_refs) if refs else []
    notes = []
    for i, (ev, r) in enumerate(zip(ref_events, refs)):
        row = {"id": ev.id, "name": ev.name, "matched": i in match}
        if i in match:
            e = estimates[match[i]]
            end = float(ev.extra.get("sound_end_seconds", ev.offset_seconds))
            f_ref = measured_f0(r, sr, ev.pitch_hz, ev.onset_seconds)
            f_est = measured_f0(e, sr, ev.pitch_hz, ev.onset_seconds)
            row.update({"siSdr": si_sdr(e, r), **{k: proj[i][k] for k in ("sdr", "sir", "sar")},
                        "pitchErrorCents": float(1200 * np.log2(f_est / f_ref)) if f_ref > 0 and f_est > 0 else float("nan"),
                        **transient_metrics(e, r, sr, ev.onset_seconds),
                        "logSpectralDistanceDb": log_spectral_distance(e, r, sr, ev.onset_seconds, end),
                        "mixtureSiSdr": si_sdr(mix, r)})
        notes.append(row)
    matched = [n for n in notes if n["matched"]]
    agg = lambda key: float(np.nanmedian([n[key] for n in matched])) if matched else float("nan")
    out = {"notes": notes, "missed": len(refs) - len(match), "falseNotes": len(est_events) - len(match),
           "siSdrMedian": agg("siSdr"), "sirMedian": agg("sir"), "sarMedian": agg("sar"),
           "pitchErrorCentsMedianAbs": float(np.nanmedian([abs(n["pitchErrorCents"]) for n in matched])) if matched else float("nan"),
           "attackEnergyErrorDbMedian": agg("attackEnergyErrorDb"), "envelopeCorrelationMedian": agg("envelopeCorrelation"),
           "logSpectralDistanceDbMedian": agg("logSpectralDistanceDb"),
           "siSdrImprovementMedian": _improvement(matched)}
    if residual is not None and residual_truth is not None:
        out["residualSiSdr"] = si_sdr(residual, residual_truth)
    return out
