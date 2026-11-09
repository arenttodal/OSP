"""Method C's attack handling, in the time domain after the long-window separation.

The 8192-point window (170 ms) that resolves close chord tones also smears every attack across
170 ms, and the hammer's noise is broadband: the harmonic model hands it to the residual's smooth
bands. A note cut out that way starts soft and dull, and carries pre-echo (smeared energy before
its onset that only cancelled out in the mixture).

C fixes both while keeping the sum exact:
  1. Pre-onset gate. Whatever a note estimate holds before (onset - pre_gate) moves to the
     residual (2 ms raised-cosine edge).
  2. Attack allocation. Around each group of notes struck together (onsets within 30 ms), the
     residual's short-window STFT (1024 / 128) is shared among the group's notes for the first
     ~100 ms, in proportion to each note's own short-window energy smoothed across a third of an
     octave (a note claims the noise in its own spectral region), times an envelope that is 1 at
     the attack and decays with a 25 ms time constant. What is not claimed stays in the residual.
Both steps only move signal between estimates, so notes + residual still equal the mixture.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.ndimage import uniform_filter1d

from ..stft import Stft
from .templates import NoteTemplate


@dataclass
class TransientConfig:
    n_fft: int = 1024
    hop: int = 128
    group_seconds: float = 0.03
    pre_gate_seconds: float = 0.005
    gate_ramp_seconds: float = 0.002
    attack_hold_seconds: float = 0.015
    attack_decay_seconds: float = 0.025
    attack_span_seconds: float = 0.12
    smooth_octaves: float = 1.0 / 3.0
    eps: float = 1e-12


def pre_onset_gate(notes: list[np.ndarray], residual: np.ndarray, templates: list[NoteTemplate], sr: int,
                   cfg: TransientConfig) -> tuple[list[np.ndarray], np.ndarray]:
    out, res = [], residual.copy()
    for x, t in zip(notes, templates):
        n = len(x)
        g = np.zeros(n)
        start = t.event.onset_seconds - cfg.pre_gate_seconds
        a = int(round(start * sr))
        r = max(1, int(round(cfg.gate_ramp_seconds * sr)))
        a0, a1 = max(0, a - r), max(0, a)
        if a1 > a0:
            g[a0:a1] = 0.5 - 0.5 * np.cos(np.pi * np.arange(a1 - a0) / (a1 - a0))
        g[a1:] = 1.0
        y = x * g
        res += x - y
        out.append(y)
    return out, res


def _onset_groups(templates: list[NoteTemplate], span: float) -> list[list[int]]:
    order = sorted(range(len(templates)), key=lambda i: templates[i].event.onset_seconds)
    groups: list[list[int]] = []
    for i in order:
        if groups and templates[i].event.onset_seconds - templates[groups[-1][0]].event.onset_seconds <= span:
            groups[-1].append(i)
        else:
            groups.append([i])
    return groups


def _smooth_log_freq(P: np.ndarray, bin_hz: float, octaves: float) -> np.ndarray:
    """Smooths a power spectrogram across frequency with a width proportional to frequency."""
    n = P.shape[0]
    f = np.arange(n) * bin_hz
    out = np.empty_like(P)
    # a few fixed widths applied band by band (cheap and close enough for an allocation weight)
    widths = np.maximum(1, np.round(f * (2 ** (octaves / 2) - 2 ** (-octaves / 2)) / bin_hz)).astype(int)
    for w in np.unique(widths):
        sel = widths == w
        out[sel] = uniform_filter1d(P, size=int(w), axis=0, mode="nearest")[sel]
    return out


def allocate_attacks(notes: list[np.ndarray], residual: np.ndarray, templates: list[NoteTemplate], sr: int,
                     cfg: TransientConfig) -> tuple[list[np.ndarray], np.ndarray, dict]:
    n = len(residual)
    st = Stft(cfg.n_fft, cfg.hop, sr)
    R = st.forward(residual)
    times = st.times(n)
    claimed = np.zeros(R.shape)
    masks = {i: None for i in range(len(notes))}
    for g in _onset_groups(templates, cfg.group_seconds):
        t0 = min(templates[i].event.onset_seconds for i in g)
        env = np.zeros(len(times))
        rel = times - t0
        env[(rel >= -cfg.pre_gate_seconds) & (rel <= cfg.attack_hold_seconds)] = 1.0
        dec = (rel > cfg.attack_hold_seconds) & (rel <= cfg.attack_span_seconds)
        env[dec] = np.exp(-(rel[dec] - cfg.attack_hold_seconds) / cfg.attack_decay_seconds)
        frames = np.nonzero(env > 0)[0]
        if len(frames) == 0:
            continue
        weights = [_smooth_log_freq(np.abs(st.forward(notes[i])[:, frames]) ** 2, st.bin_hz, cfg.smooth_octaves) for i in g]
        total = np.sum(weights, axis=0)
        room = 1.0 - claimed[:, frames]          # groups closer than the attack span never share a bin twice
        for i, w in zip(g, weights):
            m = np.where(total > cfg.eps, env[frames][None, :] * w / (total + cfg.eps), 0.0) * room
            if masks[i] is None:
                masks[i] = np.zeros(R.shape)
            masks[i][:, frames] += m
            claimed[:, frames] += m
    out = [x.copy() for x in notes]
    res_out = residual.copy()
    moved = 0.0
    for i, M in masks.items():
        if M is None:
            continue
        y = st.inverse(M * R, n)
        out[i] += y
        res_out -= y
        moved += float(np.sum(y * y))
    return out, res_out, {"attackEnergyMoved": moved, "maxClaim": float(np.max(claimed))}
