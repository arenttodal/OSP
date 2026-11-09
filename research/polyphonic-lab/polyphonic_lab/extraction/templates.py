"""Per-note harmonic templates: where each detected note's partials actually are in the mixture.

A note event only says "MIDI 60 from 0.1 s to 2.1 s". To take that note's energy out of a chord,
the separation needs the frequency of every partial, and a struck string's partials are not at
k * f0: string stiffness pushes them sharp, f_k = k f0 sqrt(1 + B k^2) (B is the inharmonicity).
On a real piano partial 24 of a mid note is a quarter tone or more above 24 f0.

So for each note this module
  1. averages the mixture's magnitude spectrum over the note's first half second;
  2. tries a few stiffness values B and keeps the one whose predicted partials carry most energy;
  3. finds the actual spectral peak near each predicted partial (parabolic interpolation);
  4. refits f0 and B by least squares on the peaks no other note can claim
     ((f_k / k)^2 = f0^2 + f0^2 B k^2 is linear in k^2), and keeps a measured peak only when it
     agrees with the fit.
The result is the note's tuning (cents), B, and one frequency per partial.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..transcription.events import NoteEvent, midi_to_hz

STIFFNESS_GRID = (0.0, 5e-5, 1.5e-4, 4e-4, 9e-4)


@dataclass
class TemplateConfig:
    max_partials: int = 60
    max_hz: float = 16000.0
    search_cents: float = 30.0        # a measured peak must lie this close to the prediction (or 1.5 bins)
    analysis_seconds: float = 0.5     # the part of the note the template is measured on
    skip_attack_seconds: float = 0.03
    claim_bins: float = 1.5           # another note's partial this close makes a peak "shared"
    min_peak_db: float = -70.0        # relative to the strongest bin of the averaged spectrum


@dataclass
class NoteTemplate:
    event: NoteEvent
    f0: float                    # refined fundamental (Hz)
    cents: float                 # f0 against the event's nominal MIDI pitch
    stiffness: float             # inharmonicity B
    partials_hz: np.ndarray      # one frequency per partial (k = 1..K)
    measured: np.ndarray         # bool: the frequency is a measured peak (not only the fit)
    amplitudes: np.ndarray       # the averaged magnitude at each partial (relative)
    extra: dict = field(default_factory=dict)

    @property
    def k(self) -> np.ndarray:
        return np.arange(1, len(self.partials_hz) + 1)


def inharmonic(f0: float, b: float, k: np.ndarray) -> np.ndarray:
    return k * f0 * np.sqrt(1.0 + b * k * k)


def _peak_near(mag: np.ndarray, bin_hz: float, hz: float, tol_hz: float) -> tuple[float, float] | None:
    """The interpolated frequency and magnitude of the largest local maximum within +-tol_hz."""
    lo = max(1, int(np.floor((hz - tol_hz) / bin_hz)))
    hi = min(len(mag) - 2, int(np.ceil((hz + tol_hz) / bin_hz)))
    if hi < lo:
        return None
    seg = mag[lo:hi + 1]
    i = lo + int(np.argmax(seg))
    if not (mag[i] >= mag[i - 1] and mag[i] >= mag[i + 1]) or mag[i] <= 0:
        return None
    a, b, c = np.log(mag[i - 1] + 1e-20), np.log(mag[i] + 1e-20), np.log(mag[i + 1] + 1e-20)
    den = a - 2 * b + c
    d = 0.5 * (a - c) / den if den < 0 else 0.0
    d = float(np.clip(d, -0.5, 0.5))
    return (i + d) * bin_hz, float(np.exp(b - 0.25 * (a - c) * d))


def _mean_spectrum(mag: np.ndarray, times: np.ndarray, ev: NoteEvent, cfg: TemplateConfig) -> np.ndarray:
    t0 = ev.onset_seconds + cfg.skip_attack_seconds
    t1 = min(ev.onset_seconds + cfg.analysis_seconds, max(ev.offset_seconds, t0 + 0.05))
    sel = (times >= t0) & (times <= t1)
    if not np.any(sel):
        sel = np.abs(times - t0) == np.min(np.abs(times - t0))
    return mag[:, sel].mean(axis=1)


def build_templates(mag: np.ndarray, times: np.ndarray, bin_hz: float, events: list[NoteEvent],
                    cfg: TemplateConfig | None = None) -> list[NoteTemplate]:
    """mag: |STFT| of the mixture (bins, frames); times: frame centres (seconds)."""
    cfg = cfg or TemplateConfig()
    nyq = bin_hz * (mag.shape[0] - 1)
    max_hz = min(cfg.max_hz, 0.95 * nyq)
    spectra = [_mean_spectrum(mag, times, ev, cfg) for ev in events]
    # Nominal partials of every note: a measured peak near someone else's partial is "shared" and
    # is not used to fit this note's tuning.
    nominal = [inharmonic(midi_to_hz(ev.pitch_midi), 2e-4, np.arange(1, cfg.max_partials + 1)) for ev in events]
    out = []
    for i, (ev, spec) in enumerate(zip(events, spectra)):
        f_nom = midi_to_hz(ev.pitch_midi)
        floor = np.max(spec) * 10 ** (cfg.min_peak_db / 20) if np.max(spec) > 0 else 0.0
        others = [nominal[j] for j, e in enumerate(events) if j != i and _overlap(e, ev)]
        other_hz = np.concatenate(others) if others else np.zeros(0)
        # 1. tuning (+-50 cents) and stiffness: the candidate whose partials carry most energy
        best = (-1.0, f_nom, 0.0)
        for cents in np.arange(-50, 51, 5):
            f0 = f_nom * 2 ** (cents / 1200)
            for b in STIFFNESS_GRID:
                k = np.arange(1, cfg.max_partials + 1)
                hz = inharmonic(f0, b, k)
                hz = hz[hz < max_hz][:20]
                idx = np.clip(np.round(hz / bin_hz).astype(int), 0, len(spec) - 1)
                score = float(np.sum(np.sqrt(spec[idx])) - 0.002 * abs(cents))
                if score > best[0]:
                    best = (score, f0, b)
        _, f0, b = best
        # 2./3. measured peaks around the prediction, and a least-squares refit on unshared ones
        for _ in range(2):
            k_all = np.arange(1, cfg.max_partials + 1)
            pred = inharmonic(f0, b, k_all)
            k_all, pred = k_all[pred < max_hz], pred[pred < max_hz]
            found, mags, ok = np.zeros(len(pred)), np.zeros(len(pred)), np.zeros(len(pred), bool)
            for j, (kk, hz) in enumerate(zip(k_all, pred)):
                tol = max(1.5 * bin_hz, hz * (2 ** (cfg.search_cents / 1200) - 1))
                pk = _peak_near(spec, bin_hz, hz, tol)
                if pk is None or pk[1] < floor:
                    continue
                found[j], mags[j], ok[j] = pk[0], pk[1], True
            shared = np.array([other_hz.size > 0 and np.min(np.abs(other_hz - hz)) < cfg.claim_bins * bin_hz for hz in pred])
            fit = ok & ~shared
            if np.count_nonzero(fit) >= 3:
                kk = k_all[fit].astype(float)
                y = (found[fit] / kk) ** 2
                w = np.sqrt(mags[fit])
                A = np.stack([np.ones_like(kk), kk * kk], axis=1) * w[:, None]
                sol, *_ = np.linalg.lstsq(A, y * w, rcond=None)
                if sol[0] > 0:
                    f0_new = float(np.sqrt(sol[0]))
                    b_new = float(np.clip(sol[1] / sol[0], 0.0, 2e-3))
                    if abs(1200 * np.log2(f0_new / f_nom)) <= 60:
                        f0, b = f0_new, b_new
        pred = inharmonic(f0, b, k_all)
        # keep a measured, unshared peak only when it agrees with the fit (within a third of a bin)
        agree = ok & ~shared & (np.abs(found - pred) < bin_hz / 3)
        hz = np.where(agree, found, pred)
        amp = np.array([spec[min(len(spec) - 1, int(round(h / bin_hz)))] for h in hz])
        peak = np.max(amp) if len(amp) and np.max(amp) > 0 else 1.0
        out.append(NoteTemplate(event=ev, f0=f0, cents=float(1200 * np.log2(f0 / f_nom)), stiffness=b,
                                partials_hz=hz, measured=agree, amplitudes=amp / peak,
                                extra={"shared": shared.tolist(), "partials": int(len(hz))}))
    return out


def _overlap(a: NoteEvent, b: NoteEvent, tail: float = 1.0) -> bool:
    return a.onset_seconds < b.offset_seconds + tail and b.onset_seconds < a.offset_seconds + tail
