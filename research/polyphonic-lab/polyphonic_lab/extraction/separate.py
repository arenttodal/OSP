"""The P2 pipeline: mixture + note events -> one audio signal per note, a residual, and checks.

Reconstruction uses the mixture's own phase (masked STFT, exact inverse), so the notes plus the
residual equal the mixture to numerical precision; `consistency` reports how closely.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

import numpy as np

from ..audio import fade, mono
from ..stft import Stft
from ..transcription.events import NoteEvent
from . import masks as masks_mod
from .templates import NoteTemplate, TemplateConfig, build_templates
from .transients import TransientConfig, allocate_attacks, pre_onset_gate

METHODS = ("A", "B", "C")
DEFAULT_RESIDUAL = {"A": "standard", "B": "standard", "C": "conservative"}


@dataclass
class ExtractConfig:
    n_fft: int = 8192
    hop: int = 512
    method: str = "C"
    residual: str | None = None            # None: the method's default
    templates: TemplateConfig = field(default_factory=TemplateConfig)
    masks: masks_mod.MaskConfig = field(default_factory=masks_mod.MaskConfig)
    transients: TransientConfig = field(default_factory=TransientConfig)
    preroll_seconds: float = 0.02          # trimmed note samples start this long before the onset
    fade_in_seconds: float = 0.002
    fade_out_seconds: float = 0.02
    end_floor_db: float = -60.0


@dataclass
class Extraction:
    method: str
    residual_strategy: str
    sr: int
    mixture: np.ndarray
    templates: list[NoteTemplate]
    notes: list[np.ndarray]                # full length, aligned with the mixture
    residual: np.ndarray
    mask_result: masks_mod.MaskResult
    seconds: float
    info: dict

    @property
    def reconstruction(self) -> np.ndarray:
        return np.sum(self.notes, axis=0) + self.residual if self.notes else self.residual.copy()

    def consistency(self) -> dict:
        err = self.mixture - self.reconstruction
        peak = float(np.max(np.abs(self.mixture))) or 1.0
        rms_mix = float(np.sqrt(np.mean(self.mixture ** 2))) or 1.0
        rms_err = float(np.sqrt(np.mean(err ** 2)))
        return {"maxAbsError": float(np.max(np.abs(err))), "maxErrorRelPeak": float(np.max(np.abs(err)) / peak),
                "errorDbRelMix": float(20 * np.log10(max(rms_err, 1e-300) / rms_mix))}


def sound_end(x: np.ndarray, sr: int, start_s: float, floor_db: float) -> float:
    """Where a note's 10 ms RMS envelope falls `floor_db` below its peak for the last time."""
    hop = max(1, int(0.01 * sr))
    n = len(x) // hop
    if n == 0:
        return len(x) / sr
    env = np.sqrt(np.mean(x[: n * hop].reshape(n, hop) ** 2, axis=1))
    a = max(0, int(start_s * sr) // hop)
    if a >= n or np.max(env[a:]) <= 0:
        return len(x) / sr
    peak = np.max(env[a:])
    above = np.nonzero(env[a:] > peak * 10 ** (floor_db / 20))[0]
    return float((a + above[-1] + 1) * hop / sr) if len(above) else start_s


def extract(audio: np.ndarray, sr: int, events: list[NoteEvent], cfg: ExtractConfig | None = None) -> Extraction:
    cfg = cfg or ExtractConfig()
    if cfg.method not in METHODS:
        raise ValueError(f"unknown extraction method '{cfg.method}' ({', '.join(METHODS)})")
    strategy = cfg.residual or DEFAULT_RESIDUAL[cfg.method]
    if strategy not in masks_mod.RESIDUAL_GAIN:
        raise ValueError(f"unknown residual strategy '{strategy}'")
    t_start = time.perf_counter()
    x = mono(audio)
    n = len(x)
    st = Stft(cfg.n_fft, cfg.hop, sr)
    X = st.forward(x)
    mag = np.abs(X)
    times = st.times(n)
    events = sorted(events, key=lambda e: (e.onset_seconds, e.pitch_midi))
    templates = build_templates(mag, times, st.bin_hz, events, cfg.templates)
    mc = masks_mod.MaskConfig(**{**cfg.masks.__dict__, "residual": strategy})
    if not templates:
        res = masks_mod.MaskResult([], np.ones_like(mag), [], info={"method": cfg.method})
    elif cfg.method == "A":
        res = masks_mod.simple_harmonic(mag, times, st.bin_hz, templates, mc)
    else:
        res = masks_mod.harmonic_nmf(mag, times, st.bin_hz, templates, mc)
    notes = [st.inverse(M * X, n) for M in res.note_masks]
    residual = st.inverse(res.residual_mask * X, n)
    info = {"masks": masks_mod.check_masks(res), **(res.info or {})}
    info["ownership"] = ownership(mag, st.bin_hz, templates, res, mc)
    if cfg.method == "C" and notes:
        notes, residual = pre_onset_gate(notes, residual, templates, sr, cfg.transients)
        notes, residual, tinfo = allocate_attacks(notes, residual, templates, sr, cfg.transients)
        info["transients"] = tinfo
    return Extraction(cfg.method, strategy, sr, x, templates, notes, residual, res, time.perf_counter() - t_start, info)


def ownership(mag: np.ndarray, bin_hz: float, templates: list[NoteTemplate], res: masks_mod.MaskResult,
              cfg: masks_mod.MaskConfig) -> list[float]:
    """For each note, the share of the energy under its own partials (inside its window) that its
    mask gives it: about 1 when nothing else sounds there, about 0.5 when it shares with an equal
    note. A measurable, reference-free hint of how cleanly the note could be taken out."""
    out = []
    for t, m, (a, b) in zip(templates, res.note_masks, res.windows):
        comb = np.asarray(masks_mod.partial_kernels(t, mag.shape[0], bin_hz, cfg).max(axis=1).todense()).ravel() > 0.5
        p = mag[comb, a:b] ** 2
        tot = float(np.sum(p))
        out.append(float(np.sum(m[comb, a:b] ** 2 * p) / tot) if tot > 0 else 0.0)
    return out


def trimmed(ex: Extraction, i: int, cfg: ExtractConfig | None = None) -> tuple[np.ndarray, float, float]:
    """Note i as a playable sample: from the pre-roll before its onset to where it has died away
    (60 dB under its peak), with short safety fades. Returns (audio, start_s, end_s)."""
    cfg = cfg or ExtractConfig()
    ev = ex.templates[i].event
    start = max(0.0, ev.onset_seconds - cfg.preroll_seconds)
    end = sound_end(ex.notes[i], ex.sr, ev.onset_seconds, cfg.end_floor_db)
    end = max(end, ev.onset_seconds + 0.05)
    a, b = int(round(start * ex.sr)), min(len(ex.notes[i]), int(round(end * ex.sr)))
    seg = fade(ex.notes[i][a:b], ex.sr, cfg.fade_in_seconds, cfg.fade_out_seconds)
    return seg, start, b / ex.sr
