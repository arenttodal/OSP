"""Method 2: a deterministic harmonic multi-pitch detector (no learned model).

Per frame (long window, for frequency resolution):
  1. the magnitude spectrum on a log-frequency grid (10-cent bins), local maxima widened by a
     tolerance (a partial may sit a little off its ideal place);
  2. every candidate pitch (20-cent steps) scores its partials k = 1..K at the inharmonic places
     k f0 sqrt(1 + B k^2) for a few stiffness values B (the best is kept), weighted towards the
     low partials (Klapuri's weights), times its odd-partial support (a sub-octave candidate
     finds its "odd partials" between the real ones and is penalised);
  3. pick the strongest, cancel its partials with a spectral-smoothness rule (a partial shared
     with another note keeps what exceeds the note's smooth envelope), repeat up to the maximum
     polyphony or until the next candidate is too weak relative to the first or to the file.
Across frames (temporal continuity, not per-frame notes):
  4. each pitch's salience is median-smoothed and segmented with hysteresis (on / off levels),
     short gaps bridged, too-short notes dropped;
  5. onsets snapped to peaks of a short-window onset function (the long window smears attacks),
     and a held pitch re-attacked (a repeated key) is split at its own spectral-flux peaks.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy.ndimage import maximum_filter1d, median_filter
from scipy.signal import find_peaks

from ..audio import mono
from ..stft import Stft
from .events import NoteEvent, hz_to_midi, midi_to_hz

CENTS = 10.0                     # log-grid resolution
F_LO = midi_to_hz(21) * 0.97      # A0
OCTAVES = 9.0


@dataclass
class HarmonicConfig:
    n_fft: int = 8192
    hop: int = 512
    onset_n_fft: int = 1024
    onset_hop: int = 128
    midi_min: int = 28
    midi_max: int = 100
    max_polyphony: int = 6
    partials: int = 24
    stiffness: tuple = (0.0, 1.5e-4, 4e-4, 9e-4)
    tolerance_cents: float = 35.0
    cancel_floor: float = 0.6       # cancel at least this share of every picked partial
    contained_ratio: float = 0.5
    ghost_ratio: float = 0.45        # an octave/twelfth above a sounding note needs this share of its peak strength    # a candidate whose partials all belong to a picked note needs this share of its strength
    relative_stop: float = 0.18      # stop when the next pick is below this share of the frame's first
    absolute_floor_db: float = -42.0  # ... or below this, relative to the file's strongest pick
    on_db: float = -32.0             # segmentation (relative to the pitch's own maximum within the file)
    off_db: float = -40.0
    min_note_s: float = 0.07
    bridge_gap_s: float = 0.05
    bridge_no_onset_s: float = 0.35  # a dip (beating, a shared partial) without a new attack is not two notes
    damper_slope_db_s: float = -90.0  # a released (damped) piano string falls much faster than this
    onset_snap_s: float = 0.06
    repeat_flux_ratio: float = 0.35
    onset_over_median: float = 3.0   # an attack's flux against its surroundings
    onset_min_share: float = 0.06    # ... and against the loudest attack  # a re-attack's flux must reach this share of the note's first attack
    extra: dict = field(default_factory=dict)


def grid_index(hz: float) -> float:
    return 1200.0 * np.log2(hz / F_LO) / CENTS


def _log_grid(sr: int) -> np.ndarray:
    n = int(OCTAVES * 1200 / CENTS)
    return F_LO * 2.0 ** (np.arange(n) * CENTS / 1200.0)


def log_spectrogram(x: np.ndarray, sr: int, cfg: HarmonicConfig) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    st = Stft(cfg.n_fft, cfg.hop, sr)
    S = np.abs(st.forward(x))
    f = st.freqs()
    grid = _log_grid(sr)
    grid = grid[grid < 0.45 * sr]
    # Magnitude at each grid frequency by linear interpolation between FFT bins, then the max of
    # the FFT bins inside each grid cell where the grid is coarser than the FFT (high frequencies).
    pos = grid / (sr / cfg.n_fft)
    i0 = np.floor(pos).astype(int)
    frac = pos - i0
    L = S[i0] * (1 - frac)[:, None] + S[np.minimum(i0 + 1, len(f) - 1)] * frac[:, None]
    return L, grid, st.times(len(x))


def _candidates(cfg: HarmonicConfig, grid_len: int):
    """Candidate pitch positions (grid index), and for each stiffness the partials' grid offsets."""
    c_lo = grid_index(midi_to_hz(cfg.midi_min - 0.3))
    c_hi = grid_index(midi_to_hz(cfg.midi_max + 0.3))
    cand = np.arange(int(round(c_lo)), int(round(c_hi)) + 1, 2)            # 20-cent steps
    k = np.arange(1, cfg.partials + 1)
    offsets = []
    for B in cfg.stiffness:
        offsets.append(np.round(1200 * np.log2(k * np.sqrt(1 + B * k * k)) / CENTS).astype(int))
    return cand, k, offsets


def _klapuri_weights(f0: np.ndarray, k: np.ndarray) -> np.ndarray:
    return (f0[:, None] + 27.0) / (k[None, :] * f0[:, None] + 320.0)


def sr_of(grid: np.ndarray, cfg: HarmonicConfig) -> float:
    return cfg.extra.get("sr", 48000) / cfg.n_fft


def frame_pitches(L: np.ndarray, grid: np.ndarray, cfg: HarmonicConfig) -> list[list[tuple[float, float]]]:
    """Per frame: [(fractional MIDI, salience), ...] picked iteratively."""
    n_grid, n_frames = L.shape
    cand, k, offsets = _candidates(cfg, n_grid)
    f0 = grid[np.clip(cand, 0, n_grid - 1)]
    w = _klapuri_weights(f0, k)
    tol = int(round(cfg.tolerance_cents / CENTS))
    odd = (k % 2 == 1)
    # The window's main lobe in grid cells at each frequency: a partial occupies at least that
    # much (at 65 Hz an 8192-point bin is ~150 cents wide), so cancellation must cover it.
    bin_hz = sr_of(grid, cfg)
    lobe = np.maximum(tol, np.ceil(1200 * np.log2(1 + 2.2 * bin_hz / grid) / CENTS)).astype(int)
    out: list[list[tuple[float, float]]] = []
    for t in range(n_frames):
        spec = L[:, t].copy()
        picks: list[tuple[float, float]] = []
        picked_partials: list[np.ndarray] = []
        picked_strength: list[float] = []
        first = None
        for _ in range(cfg.max_polyphony):
            wide = maximum_filter1d(spec, size=2 * tol + 1)
            scores = []
            for oi, off in enumerate(offsets):
                idx = cand[:, None] + off[None, :]
                valid = idx < n_grid
                a = np.where(valid, wide[np.minimum(idx, n_grid - 1)], 0.0)
                sc = np.sum(w * a, axis=1)
                odd_share = np.sum((w * a)[:, odd], axis=1) / (sc + 1e-12)
                sc = sc * np.clip(odd_share / 0.3, 0.0, 1.0) * np.clip(a[:, 0] / (a.max(axis=1) * 0.08 + 1e-12), 0.0, 1.0) ** 0.5
                scores.append((sc, a, idx))
            best = None
            # The strongest candidate that is not merely partials of a note already picked (a
            # twelfth or an octave above a note borrows all its "partials" from it).
            combined = np.max(np.stack([sc for sc, _, _ in scores]), axis=0)
            order = np.argsort(-combined)
            for j in order[:12]:
                oi = int(np.argmax([sc[j] for sc, _, _ in scores]))
                sj, a, idx = scores[oi][0][j], scores[oi][1][j], scores[oi][2][j]
                if sj <= 0:
                    break
                if any(abs(cand[j] - pp[0]) * CENTS < 60 for pp in picked_partials):
                    continue          # the same pitch again (its leftover)
                if picked_partials:
                    pos_j = idx[idx < n_grid]
                    shared = max(np.mean(np.min(np.abs(pos_j[:, None] - pp[None, :]), axis=1) <= tol) for pp in picked_partials)
                    if shared >= 0.8 and sj < cfg.contained_ratio * max(picked_strength):
                        continue
                best = (int(j), oi, a, float(sj))
                break
            if best is None:
                break
            j, oi, a, best_s = best
            if first is None:
                first = best_s
            elif best_s < cfg.relative_stop * first:
                break
            pos = cand[j]
            # Refine to the strongest place within +-tol of the fundamental, the pitch's MIDI.
            lo, hi = max(0, pos - tol), min(n_grid, pos + tol + 1)
            fund = lo + int(np.argmax(spec[lo:hi])) if hi > lo else pos
            midi = hz_to_midi(float(grid[min(fund, n_grid - 1)]))
            picks.append((float(midi), best_s))
            picked_partials.append(np.array([fund + o for o in offsets[oi] if fund + o < n_grid]))
            picked_strength.append(best_s)
            # Cancel its partials, keeping what exceeds its smooth envelope (shared partials stay),
            # over the window's main lobe so its skirts are not taken for neighbouring notes.
            amp = a.copy()
            smooth = np.minimum(amp, np.convolve(np.pad(amp, 1, mode="edge"), np.ones(3) / 3, mode="valid"))
            # A real piano's partials are not smooth (the hammer's comb): take at least
            # `cancel_floor` of every partial, or its leftovers become octave / twelfth ghosts.
            smooth = np.minimum(amp, np.maximum(smooth, cfg.cancel_floor * amp))
            actual = []
            for kk, off in enumerate(offsets[oi]):
                c = fund + off
                if c >= n_grid:
                    break
                # At the partial's real peak near its predicted place (stiffness is only estimated).
                lo, hi = max(0, c - tol), min(n_grid, c + tol + 1)
                c = lo + int(np.argmax(spec[lo:hi]))
                actual.append(c)
                half = int(lobe[min(c, n_grid - 1)])
                lo, hi = max(0, c - half), min(n_grid, c + half + 1)
                spec[lo:hi] = np.maximum(0.0, spec[lo:hi] - smooth[kk])
            if actual:
                picked_partials[-1] = np.array(actual)
        out.append(picks)
    return out


def onset_function(x: np.ndarray, sr: int, cfg: HarmonicConfig) -> tuple[np.ndarray, np.ndarray]:
    st = Stft(cfg.onset_n_fft, cfg.onset_hop, sr)
    S = np.log1p(100.0 * np.abs(st.forward(x)))
    flux = np.maximum(0.0, np.diff(S, axis=1, prepend=S[:, :1])).sum(axis=0)
    return flux, st.times(len(x))


def onset_peaks(flux: np.ndarray, ftimes: np.ndarray, sr: int, cfg: HarmonicConfig) -> np.ndarray:
    """Attacks: flux peaks well above their surroundings (a moving median over ~0.3 s), and not
    tiny against the loudest attack (vibrato and beating ripple the flux; they are not attacks)."""
    if flux.max() <= 0:
        return np.zeros(0)
    width = max(3, int(0.3 * sr / cfg.onset_hop)) | 1
    local = median_filter(flux, size=width)
    keep = (flux > local * cfg.onset_over_median) & (flux > flux.max() * cfg.onset_min_share)
    peaks, _ = find_peaks(np.where(keep, flux, 0.0), distance=max(1, int(0.03 * sr / cfg.onset_hop)))
    return ftimes[peaks]


def detect(audio: np.ndarray, sr: int, cfg: HarmonicConfig | None = None) -> list[NoteEvent]:
    cfg = cfg or HarmonicConfig()
    cfg.extra["sr"] = sr
    x = mono(audio)
    L, grid, times = log_spectrogram(x, sr, cfg)
    picks = frame_pitches(L, grid, cfg)
    n_frames = len(picks)
    keys = np.arange(cfg.midi_min, cfg.midi_max + 1)
    A = np.zeros((len(keys), n_frames))
    cents = np.zeros((len(keys), n_frames))
    for t, ps in enumerate(picks):
        for midi, s in ps:
            key = int(round(midi))
            if cfg.midi_min <= key <= cfg.midi_max:
                i = key - cfg.midi_min
                if s > A[i, t]:
                    A[i, t] = s
                    cents[i, t] = (midi - key) * 100
    file_max = A.max() if A.size else 0.0
    if file_max <= 0:
        return []
    A[A < file_max * 10 ** (cfg.absolute_floor_db / 20)] = 0.0
    A = median_filter(A, size=(1, 5))
    flux, ftimes = onset_function(x, sr, cfg)
    onset_times = onset_peaks(flux, ftimes, sr, cfg)
    hop_s = cfg.hop / sr
    events: list[NoteEvent] = []
    for i, key in enumerate(keys):
        a = A[i]
        peak = a.max()
        if peak <= 0:
            continue
        on = a > peak * 10 ** (cfg.on_db / 20)
        off = a > peak * 10 ** (cfg.off_db / 20)
        segs = _hysteresis(on, off)
        segs = _bridge(segs, int(round(cfg.bridge_gap_s / hop_s)))
        segs = _bridge_without_onset(segs, times, onset_times, int(round(cfg.bridge_no_onset_s / hop_s)), cfg.n_fft / sr / 2 + cfg.onset_snap_s)
        for s0, s1 in segs:
            if (s1 - s0) * hop_s < cfg.min_note_s:
                continue
            t0 = float(times[s0])
            t1 = _offset_at_damper(a, s0, min(s1, n_frames), times, cfg, sr)
            # Snap the onset to the short window's attack (the long window starts early: it sees
            # the attack half a window before its centre).
            # A long frame centred at t0 already sees an attack up to half a window later.
            half = cfg.n_fft / sr / 2
            near = onset_times[(onset_times > t0 - cfg.onset_snap_s) & (onset_times < t0 + half + cfg.onset_snap_s)]
            if len(near):
                t0 = float(near[np.argmin(np.abs(near - (t0 + 0.5 * half)))])
            # A held key struck again: split at its own harmonic flux peaks within the segment.
            splits = [t for t in _repeat_splits(L, grid, key, s0, s1, times, onset_times, cfg, sr) if t > t0 + cfg.min_note_s]
            bounds = [t0] + splits + [t1]
            for b0, b1 in zip(bounds[:-1], bounds[1:]):
                if b1 - b0 < cfg.min_note_s:
                    continue
                f0 = s0 + int((b0 - t0) / hop_s)
                f1 = max(f0 + 1, s0 + int((b1 - t0) / hop_s))
                seg = a[f0:f1]
                c = cents[i, f0:f1][seg > 0]
                events.append(NoteEvent(id="", pitch_midi=int(key), onset_seconds=round(max(0.0, b0), 4), offset_seconds=round(b1, 4),
                                        pitch_hz=midi_to_hz(key + (float(np.median(c)) / 100 if len(c) else 0.0)),
                                        velocity_estimate=float(np.clip((seg.max() / file_max) ** 0.5, 0, 1)) if len(seg) else 0.0,
                                        confidence=float(np.clip(np.mean(seg > 0) * min(1.0, (b1 - b0) / 0.3), 0, 1)) if len(seg) else 0.0,
                                        detector="harmonic", extra={"peak_salience": float(seg.max()) if len(seg) else 0.0}))
    events = remove_harmonic_ghosts(events, onset_times, cfg, sr)
    events.sort(key=lambda e: (e.onset_seconds, e.pitch_midi))
    for n, e in enumerate(events):
        e.id = f"note-{n + 1:04d}"
    return events


def _bridge_without_onset(segs, times, onset_times, gap, reach) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for s in segs:
        if out and s[0] - out[-1][1] <= gap:
            t = float(times[min(s[0], len(times) - 1)])
            if not np.any(np.abs(onset_times - t) < reach):
                out[-1] = (out[-1][0], s[1])
                continue
        out.append(s)
    return out


def _offset_at_damper(a: np.ndarray, s0: int, s1: int, times: np.ndarray, cfg: HarmonicConfig, sr: int) -> float:
    """The key release: where the pitch's level starts to fall much faster than a free string
    decays (the damper), else the segment's end; corrected for the long window's lag."""
    seg = a[s0:s1]
    end = float(times[min(s1, len(times) - 1)])
    if len(seg) < 6:
        return end
    db = 20 * np.log10(seg + 1e-12)
    hop_s = cfg.hop / sr
    slope = np.gradient(median_filter(db, size=3)) / hop_s
    steep = np.nonzero(slope < cfg.damper_slope_db_s)[0]
    steep = steep[steep > len(seg) // 6]
    if len(steep) == 0:
        return end
    # The first frame of the last steep run (the release), not a dip in the middle of the note.
    runs = np.split(steep, np.nonzero(np.diff(steep) > 2)[0] + 1)
    k = int(runs[-1][0])
    return float(times[s0 + k]) + 0.25 * cfg.n_fft / sr / 2 * 0.0 - 0.0


GHOST_INTERVALS = (12, 19, 24, 28, 31, 36)   # partials 2, 3, 4, 5, 6, 8 of a lower note


def remove_harmonic_ghosts(events: list[NoteEvent], onset_times: np.ndarray, cfg: HarmonicConfig, sr: int) -> list[NoteEvent]:
    """A note an octave / twelfth / ... above a sounding note is that note's partials (a beating
    fundamental lets them win a frame) unless it has its own attack and real strength: drop it
    when it starts without an attack while the lower note sounds, or when it is much weaker."""
    reach = cfg.onset_snap_s + cfg.n_fft / sr / 4
    keep = []
    for e in events:
        ghost = False
        for lo in events:
            if lo is e or (e.pitch_midi - lo.pitch_midi) not in GHOST_INTERVALS:
                continue
            if not (lo.onset_seconds <= e.onset_seconds + 0.03 and lo.offset_seconds >= e.onset_seconds + 0.05):
                continue
            attacked = bool(np.any(np.abs(onset_times - e.onset_seconds) < reach))
            late = e.onset_seconds > lo.onset_seconds + 0.08
            weak = e.extra.get("peak_salience", 0.0) < cfg.ghost_ratio * lo.extra.get("peak_salience", 0.0)
            if (late and not attacked) or weak:
                ghost = True
                break
        if not ghost:
            keep.append(e)
    return keep


def _hysteresis(on: np.ndarray, off: np.ndarray) -> list[tuple[int, int]]:
    segs, i, n = [], 0, len(on)
    while i < n:
        if on[i]:
            s = i
            while s > 0 and off[s - 1]:
                s -= 1
            e = i
            while e < n and off[e]:
                e += 1
            segs.append((s, e))
            i = e
        else:
            i += 1
    return segs


def _bridge(segs: list[tuple[int, int]], gap: int) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for s in segs:
        if out and s[0] - out[-1][1] <= gap:
            out[-1] = (out[-1][0], s[1])
        else:
            out.append(s)
    return out


def _repeat_splits(L, grid, key, s0, s1, times, onset_times, cfg, sr) -> list[float]:
    """Re-attacks of a held key: where its own harmonics' energy jumps up again (a decaying string
    never rises by itself), confirmed by a broadband onset."""
    f0 = midi_to_hz(key)
    rows = []
    for k in (1, 2):
        target = 1200 * np.log2(k * f0 / F_LO) / CENTS
        r = int(round(target))
        if 0 <= r < L.shape[0]:
            rows.append(slice(max(0, r - 2), r + 3))
    if not rows or s1 - s0 < 6:
        return []
    e = np.array([sum(L[r, t].max() for r in rows) for t in range(s0, s1)])
    rise = np.maximum(0.0, np.diff(e, prepend=e[0]))
    first = rise[: max(3, len(rise) // 8)].max() if len(rise) else 0.0
    if first <= 0:
        return []
    pk, _ = find_peaks(rise, height=first * cfg.repeat_flux_ratio, distance=max(2, int(0.12 / (cfg.hop / sr))))
    out = []
    for p in pk:
        if p < 3:
            continue
        t = float(times[s0 + p])
        near = onset_times[np.abs(onset_times - t) < cfg.n_fft / sr / 2 + cfg.onset_snap_s]
        if len(near):
            out.append(float(near[np.argmin(np.abs(near - t))]))
    return sorted(set(round(x, 4) for x in out))
