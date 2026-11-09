"""Time-frequency masks that divide the mixture among its notes and a residual.

Every method returns one mask per note plus a residual mask. Masks are finite, lie in [0, 1] and
sum to exactly 1 in every bin, so the notes plus the residual reconstruct the mixture (mixture
phase, linear inverse STFT): nothing is invented and nothing is lost.

Method A -- simple harmonic masks.
    Each note owns a comb of window-shaped peaks at its partials, inside its activity window.
    A bin under one note's comb goes to that note; a bin under several is split by comb height;
    everything between partials goes to the residual. No model of how loud anything is.

Method B -- harmonic NMF (one component per partial), then Wiener masks.
    One component per partial: a fixed spectral shape (the analysis window's main lobe at the
    partial's measured frequency) with a free, non-negative activation over time, zero outside
    the note's activity window. A bank of broad, smooth spectral bands models what is not a
    partial (hammer noise, room, sympathetic bloom). Activations are fitted to the mixture's
    magnitude by multiplicative KL-NMF updates.
    Where two notes' partials fall in the same bins (C4's 3rd partial and G4's 2nd are 2 cents
    apart) the split rests on what still differs: a few cents of inharmonicity and each partial's
    own decay. An optional step (`smoothness`) instead re-divides shared energy by spectral
    smoothness: each colliding partial's level predicted from its own note's nearest unshared
    partials (geometric mean), as in Klapuri's multiple-F0 work. On the piano corpus it loses
    3 dB (a piano's partial levels follow the hammer's comb, not a smooth envelope), so it is off.
    Masks are the components' power ratios (Wiener).
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np
from scipy import sparse

from .templates import NoteTemplate

RESIDUAL_GAIN = {"none": 0.0, "conservative": 0.5, "standard": 1.0, "generous": 2.0}


@dataclass
class MaskConfig:
    pre_seconds: float = 0.07       # activity starts this long before the onset (the long window sees the attack early)
    tail_seconds: float = 1.2       # and continues this long after the key release (the damped tail)
    lobe_bins: float = 2.0          # the Hann main lobe half-width
    kernel_bins: int = 4            # kernel support (+- bins)
    iterations: int = 60
    residual_bands: int = 40
    band_knee_hz: float = 400.0
    residual: str = "standard"      # none | conservative | standard | generous
    smoothness: bool = False        # re-divide colliding partials by spectral smoothness (measured: worse on piano, see the P2 report)
    collide_bins: float = 1.5
    max_hz: float = 16000.0
    eps: float = 1e-12


class LazyMasks(Sequence):
    """One mask per note, made when asked for: a recording with a hundred notes would otherwise
    hold a hundred full-size spectrograms at once (16 s, 113 notes: about 5 GB)."""

    def __init__(self, make, count: int):
        self._make, self._count = make, count

    def __len__(self) -> int:
        return self._count

    def __getitem__(self, i):
        if isinstance(i, slice):
            return [self[j] for j in range(*i.indices(self._count))]
        if not -self._count <= i < self._count:
            raise IndexError(i)
        return self._make(i % self._count)


@dataclass
class MaskResult:
    note_masks: Sequence            # each (bins, frames); a list or LazyMasks
    residual_mask: np.ndarray
    windows: list[tuple[int, int]]  # each note's activity (frames, end exclusive)
    activations: list[np.ndarray] | None = None   # (partials, frames) per note (method B)
    info: dict | None = None


def activity_windows(templates: list[NoteTemplate], times: np.ndarray, cfg: MaskConfig) -> list[tuple[int, int]]:
    """A note sounds from just before its onset to its key release plus the damped tail, but a
    re-struck key ends the previous note at the new onset (it is the same string)."""
    out = []
    for i, t in enumerate(templates):
        ev = t.event
        end = ev.offset_seconds + cfg.tail_seconds
        for u in templates:
            e = u.event
            if e is not ev and e.pitch_midi == ev.pitch_midi and ev.onset_seconds < e.onset_seconds < end:
                end = e.onset_seconds - cfg.pre_seconds
        a = int(np.searchsorted(times, ev.onset_seconds - cfg.pre_seconds))
        b = int(np.searchsorted(times, end, side="right"))
        out.append((max(0, min(a, len(times) - 1)), max(a + 1, min(b, len(times)))))
    return out


def _hann_lobe(delta_bins: np.ndarray) -> np.ndarray:
    """|W(delta)| of a Hann window (normalised to 1 at delta 0)."""
    d = np.asarray(delta_bins, dtype=np.float64)
    num = np.sinc(d)
    den = 1.0 - d * d
    out = np.where(np.abs(den) < 1e-9, 0.5, num / np.where(np.abs(den) < 1e-9, 1.0, den))
    return np.abs(out)


def partial_kernels(t: NoteTemplate, n_bins: int, bin_hz: float, cfg: MaskConfig) -> sparse.csc_matrix:
    """(bins, partials): each column a window main lobe at one partial's frequency."""
    rows, cols, vals = [], [], []
    for j, hz in enumerate(t.partials_hz):
        if hz >= cfg.max_hz:
            continue
        c = hz / bin_hz
        lo, hi = max(0, int(np.floor(c)) - cfg.kernel_bins), min(n_bins - 1, int(np.ceil(c)) + cfg.kernel_bins)
        b = np.arange(lo, hi + 1)
        v = _hann_lobe(b - c)
        v[np.abs(b - c) > cfg.lobe_bins + 0.5] = 0.0
        keep = v > 1e-4
        rows.extend(b[keep])
        cols.extend([j] * int(np.count_nonzero(keep)))
        vals.extend(v[keep])
    return sparse.csc_matrix((vals, (rows, cols)), shape=(n_bins, len(t.partials_hz)))


def residual_bands(n_bins: int, bin_hz: float, cfg: MaskConfig) -> np.ndarray:
    """(bins, bands): overlapping raised-cosine bands, broad and smooth so that they cannot fit a
    narrow partial: harmonic energy stays with the notes."""
    f = np.arange(n_bins) * bin_hz
    # spaced on log(1 + f / band_knee_hz): log-like up high, but never narrower than about
    # band_knee_hz / 4 at the bottom, where a log band would be as narrow as a low note's partial
    warp = lambda hz: np.log1p(np.asarray(hz) / cfg.band_knee_hz)
    lf = warp(f)
    le = np.linspace(warp(0.0), warp(min(cfg.max_hz, f[-1])), cfg.residual_bands + 2)
    out = np.zeros((n_bins, cfg.residual_bands))
    for i in range(cfg.residual_bands):
        lo, mid, hi = le[i], le[i + 1], le[i + 2]
        up = (lf >= lo) & (lf <= mid)
        dn = (lf > mid) & (lf <= hi)
        out[up, i] = 0.5 - 0.5 * np.cos(np.pi * (lf[up] - lo) / (mid - lo))
        out[dn, i] = 0.5 + 0.5 * np.cos(np.pi * (lf[dn] - mid) / (hi - mid))
    return out


def simple_harmonic(mag: np.ndarray, times: np.ndarray, bin_hz: float, templates: list[NoteTemplate],
                    cfg: MaskConfig | None = None) -> MaskResult:
    """Method A."""
    cfg = cfg or MaskConfig()
    n_bins, n_frames = mag.shape
    windows = activity_windows(templates, times, cfg)
    combs = [np.asarray(partial_kernels(t, n_bins, bin_hz, cfg).max(axis=1).todense()).ravel() for t in templates]
    total = np.zeros((n_bins, n_frames))
    for comb, (a, b) in zip(combs, windows):
        total[:, a:b] += comb[:, None]
    scale = np.maximum(total, 1.0)

    def make(i):
        a, b = windows[i]
        m = np.zeros((n_bins, n_frames))
        m[:, a:b] = combs[i][:, None] / scale[:, a:b]
        return m

    residual = 1.0 - (total / scale)
    return MaskResult(LazyMasks(make, len(templates)), np.clip(residual, 0.0, 1.0), windows, info={"method": "A"})


def _smoothness(H: list[np.ndarray], templates: list[NoteTemplate], windows, bin_hz: float, cfg: MaskConfig) -> dict:
    """Re-divides energy among partials that share bins (see the module docstring). H is modified
    in place; returns counts for the report."""
    entries = []   # (note, partial, hz)
    for n, t in enumerate(templates):
        for k, hz in enumerate(t.partials_hz):
            if hz < cfg.max_hz:
                entries.append((n, k, hz))
    if not entries:
        return {"collisions": 0}
    hz = np.array([e[2] for e in entries])
    order = np.argsort(hz)
    groups, cur = [], [order[0]]
    for i in order[1:]:
        if hz[i] - hz[cur[-1]] < cfg.collide_bins * bin_hz:
            cur.append(i)
        else:
            groups.append(cur)
            cur = [i]
    groups.append(cur)
    collided = set()
    for g in groups:
        notes = {entries[i][0] for i in g}
        if len(notes) < 2:
            continue
        for i in g:
            collided.add((entries[i][0], entries[i][1]))
    n_fixed = 0
    for g in groups:
        members = [entries[i] for i in g]
        if len({m[0] for m in members}) < 2:
            continue
        total = np.sum([H[n][k] for n, k, _ in members], axis=0)
        est = []
        for n, k, _ in members:
            nb = []
            for step in (-1, 1):
                for d in range(1, 4):
                    j = k + step * d
                    if 0 <= j < H[n].shape[0] and (n, j) not in collided and templates[n].partials_hz[j] < cfg.max_hz:
                        nb.append(H[n][j])
                        break
            if nb:
                e = np.exp(np.mean([np.log(x + cfg.eps) for x in nb], axis=0))
                a, b = windows[n]
                mask = np.zeros_like(e)
                mask[a:b] = 1.0
                est.append(e * mask)
            else:
                est.append(H[n][k].copy())
        est = np.array(est)
        s = est.sum(axis=0)
        share = np.where(s > cfg.eps, est / np.maximum(s, cfg.eps), 1.0 / len(members))
        for (n, k, _), sh in zip(members, share):
            H[n][k] = total * sh
            n_fixed += 1
    return {"collisions": n_fixed, "collisionGroups": sum(1 for g in groups if len({entries[i][0] for i in g}) > 1)}


def harmonic_nmf(mag: np.ndarray, times: np.ndarray, bin_hz: float, templates: list[NoteTemplate],
                 cfg: MaskConfig | None = None) -> MaskResult:
    """Method B."""
    cfg = cfg or MaskConfig()
    if cfg.residual not in RESIDUAL_GAIN:
        raise ValueError(f"unknown residual strategy '{cfg.residual}' (none, conservative, standard, generous)")
    n_bins, n_frames = mag.shape
    windows = activity_windows(templates, times, cfg)
    kernels = [partial_kernels(t, n_bins, bin_hz, cfg) for t in templates]
    bands = residual_bands(n_bins, bin_hz, cfg)
    W = sparse.hstack(kernels + [sparse.csc_matrix(bands)]).tocsr() if kernels else sparse.csr_matrix(bands)
    WT = W.T.tocsr()
    sizes = [k.shape[1] for k in kernels]
    n_comp = W.shape[1]
    # activity: partials only inside their note's window; residual bands everywhere
    A = np.zeros((n_comp, n_frames))
    row = 0
    for s, (a, b) in zip(sizes, windows):
        A[row:row + s, a:b] = 1.0
        row += s
    A[row:, :] = 1.0
    V = mag + cfg.eps
    col_sum = np.asarray(W.sum(axis=0)).ravel()[:, None] + cfg.eps
    H = A * (WT @ V) / col_sum / max(1.0, float(np.max(W.sum(axis=1))))
    H = np.maximum(H, cfg.eps) * A
    for _ in range(cfg.iterations):
        WH = W @ H + cfg.eps
        H *= (WT @ (V / WH)) / col_sum
        H *= A
    Hn, row = [], 0
    for s in sizes:
        Hn.append(H[row:row + s].copy())
        row += s
    Hr = H[row:]
    info = _smoothness(Hn, templates, windows, bin_hz, cfg) if cfg.smoothness else {"collisions": 0}
    # Wiener masks on power
    gain = RESIDUAL_GAIN[cfg.residual]
    note_pow = lambda i: np.asarray(kernels[i] @ Hn[i]) ** 2
    claim = np.zeros_like(mag)
    for i in range(len(kernels)):
        claim += note_pow(i)
    res_pow = (gain * (bands @ Hr)) ** 2
    denom = claim + res_pow + cfg.eps
    if cfg.residual == "none":
        # nothing goes to the residual where a note is active and has any claim
        has = claim > 1e-6 * (np.max(claim) + cfg.eps)
        divisor = np.where(has, np.maximum(claim, cfg.eps), denom)
    else:
        divisor = denom
    residual = 1.0 - claim / divisor
    del claim, denom
    make = lambda i: note_pow(i) / divisor
    residual = np.clip(residual, 0.0, 1.0)
    info.update({"method": "B", "components": int(n_comp), "iterations": cfg.iterations, "residual": cfg.residual,
                 "klDivergence": float(_kl(V, W @ H + cfg.eps))})
    return MaskResult(LazyMasks(make, len(kernels)), residual, windows, activations=Hn, info=info)


def _kl(V: np.ndarray, WH: np.ndarray) -> float:
    return float(np.sum(V * np.log(V / WH) - V + WH) / np.sum(V))


def check_masks(res: MaskResult, atol: float = 1e-9) -> dict:
    """Finite, within [0, 1], and summing to one (the consistency the reconstruction relies on)."""
    s = res.residual_mask.copy()
    finite, in_range = True, True
    for m in [res.residual_mask, *res.note_masks]:
        finite &= bool(np.all(np.isfinite(m)))
        in_range &= bool(np.min(m) >= -atol and np.max(m) <= 1 + atol)
    for m in res.note_masks:
        s += m
    return {"finite": finite, "inRange": in_range, "sumMaxError": float(np.max(np.abs(s - 1.0)))}
