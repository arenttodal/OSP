"""Deterministic instrument synthesis for the reference corpus (exact ground truth).

The piano is physically informed, not a sample library:
  * inharmonic partials  f_k = k f0 sqrt(1 + B k^2), with string stiffness B rising with register;
  * 1-3 unison strings a fraction of a cent apart (beating, as a real piano's choirs);
  * a hammer-position comb (the struck point at about 1/8 of the string) and a velocity-dependent
    spectral slope (harder notes are brighter);
  * a two-stage decay per partial (fast prompt sound, slow aftersound), shorter for high partials;
  * a hammer-noise transient (band-passed noise, a few ms) and the damper on key release;
  * with the sustain pedal, no damper and quiet sympathetic resonance of related strings.

Everything is a function of its arguments and a seed (numpy PCG64): byte-identical output on one
machine; across platforms the libm differences stay far below any metric's resolution.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.signal import butter, sosfilt

from ..transcription.events import midi_to_hz

SR = 48000


@dataclass
class Played:
    """One note of a fixture: its key, when it is struck and released, how hard."""
    midi: int
    onset: float
    offset: float          # key release (seconds); the sound continues into its release tail
    velocity: float = 0.75
    instrument: str = "piano"


def _rng(seed: int, *salt: int) -> np.random.Generator:
    return np.random.Generator(np.random.PCG64([seed, *salt]))


def piano_note(p: Played, total_s: float, sr: int = SR, seed: int = 1, pedal: bool = False) -> np.ndarray:
    """One piano note as its own stem over the whole fixture (mono)."""
    rng = _rng(seed, p.midi, int(round(p.onset * 1000)))
    n_total = int(round(total_s * sr))
    start = int(round(p.onset * sr))
    n = n_total - start
    if n <= 0:
        return np.zeros(n_total)
    t = np.arange(n) / sr
    f0 = midi_to_hz(p.midi)
    v = float(np.clip(p.velocity, 0.05, 1.0))
    stiffness = 4.0e-5 * 2.0 ** ((p.midi - 21) / 12.0 * 0.55)
    strings = 1 if p.midi < 35 else (2 if p.midi < 48 else 3)
    detune = rng.uniform(-1.0, 1.0, strings) * (0.4 + 0.6 * rng.random())   # cents
    if strings > 1:
        detune -= detune.mean()
    hammer_at = 0.115 + 0.02 * rng.random()
    slope = 1.85 - 0.85 * v
    cutoff = 2500.0 + 7000.0 * v
    held = max(0.0, p.offset - p.onset)
    # Decay times at this register (the fundamental): prompt and aftersound.
    tau_prompt = 0.35 * (f0 / 261.6) ** -0.55
    tau_after = 3.2 * (f0 / 261.6) ** -0.75
    damper = None
    if not pedal:
        damper = np.ones(n)
        k_off = int(round(held * sr))
        if k_off < n:
            tau_r = 0.07 + (0.08 if p.midi < 48 else 0.0)
            damper[k_off:] = np.exp(-(np.arange(n - k_off)) / (tau_r * sr))
    out = np.zeros(n)
    k = 1
    while True:
        fk = k * f0 * np.sqrt(1.0 + stiffness * k * k)
        if fk > 16000.0 or k > 80:
            break
        a = (abs(np.sin(np.pi * k * hammer_at)) + 0.04) * k ** -slope * np.exp(-fk / cutoff)
        tp = tau_prompt / (1.0 + 0.03 * k)
        ta = tau_after / (1.0 + 0.06 * k)
        env = 0.72 * np.exp(-t / tp) + 0.28 * np.exp(-t / ta)
        attack = np.minimum(1.0, t / 0.0015)
        for s in range(strings):
            fs = fk * 2.0 ** (detune[s] / 1200.0)
            out += (a / strings) * np.sin(2 * np.pi * fs * t + rng.uniform(0, 2 * np.pi)) * env * attack
        k += 1
    # The hammer: a short band-passed noise burst, louder and brighter for harder notes.
    noise = rng.standard_normal(n)
    lo, hi = 400.0, min(0.45 * sr, 2000.0 + 6000.0 * v)
    noise = sosfilt(butter(2, [lo, hi], btype="band", fs=sr, output="sos"), noise)
    out += 0.06 * v ** 1.5 * noise * np.exp(-t / 0.005) * (out.std() / (noise.std() + 1e-12))
    if damper is not None:
        out *= damper
    level = 0.22 * v ** 1.6
    out *= level / (np.max(np.abs(out)) + 1e-12)
    stem = np.zeros(n_total)
    stem[start:] = out
    return stem


def sympathetic(notes: list[Played], total_s: float, sr: int = SR, seed: int = 9) -> np.ndarray:
    """With the pedal down every string can ring: quiet resonance an octave and a twelfth above
    each struck note's strings (ground truth for the residual, never a note)."""
    out = np.zeros(int(round(total_s * sr)))
    for p in notes:
        # (about -30 dB under the struck note: audible as bloom, never as a note)
        for interval, gain in ((12, 0.12), (19, 0.08), (-12, 0.1)):
            q = Played(p.midi + interval, p.onset + 0.02, total_s, velocity=0.35)
            if 21 <= q.midi <= 108:
                out += gain * piano_note(q, total_s, sr, seed + 100 + interval, pedal=True)
    return out


def organ_note(p: Played, total_s: float, sr: int = SR, seed: int = 2) -> np.ndarray:
    """Drawbar-style organ: stationary harmonics (8', 4', 2 2/3', 2'), a key click, no decay."""
    rng = _rng(seed, p.midi)
    n_total = int(round(total_s * sr))
    out = np.zeros(n_total)
    a, b = int(round(p.onset * sr)), min(n_total, int(round(p.offset * sr)))
    t = np.arange(b - a) / sr
    f0 = midi_to_hz(p.midi)
    tone = np.zeros(b - a)
    for mult, gain in ((1, 1.0), (2, 0.7), (3, 0.5), (4, 0.4), (6, 0.15), (8, 0.1)):
        tone += gain * np.sin(2 * np.pi * f0 * mult * t + rng.uniform(0, 2 * np.pi))
    env = np.minimum(1.0, t / 0.008) * np.minimum(1.0, (t[-1] - t) / 0.02 + 1e-9)
    out[a:b] = 0.06 * p.velocity * tone * env
    return out


def guitar_note(p: Played, total_s: float, sr: int = SR, seed: int = 3) -> np.ndarray:
    """Karplus-Strong plucked string (a noise burst in a damped, tuned delay loop)."""
    rng = _rng(seed, p.midi, int(round(p.onset * 1000)))
    n_total = int(round(total_s * sr))
    out = np.zeros(n_total)
    a = int(round(p.onset * sr))
    period = sr / midi_to_hz(p.midi)
    d = int(np.floor(period))
    frac = period - d
    buf = rng.uniform(-1, 1, d + 2)
    y = np.zeros(n_total - a)
    loss = 0.996
    for i in range(len(y)):
        j = i % (d + 1)
        x = buf[j]
        nxt = buf[(j + 1) % (d + 1)]
        val = (1 - frac) * x + frac * nxt
        buf[j] = loss * 0.5 * (x + nxt)
        y[i] = val
    y *= np.exp(-np.arange(len(y)) / (sr * 1.6))
    out[a:] = 0.18 * p.velocity * y / (np.max(np.abs(y)) + 1e-12)
    return out


def bowed_note(p: Played, total_s: float, sr: int = SR, seed: int = 4, vibrato_cents: float = 14.0,
               formants: tuple = ()) -> np.ndarray:
    """A sustained harmonic tone with vibrato and a slow bow (strings), or with vowel formants
    and breath (a voice in a harmony)."""
    rng = _rng(seed, p.midi, int(round(p.onset * 1000)))
    n_total = int(round(total_s * sr))
    out = np.zeros(n_total)
    a, b = int(round(p.onset * sr)), min(n_total, int(round(p.offset * sr)))
    t = np.arange(b - a) / sr
    f0 = midi_to_hz(p.midi)
    rate = 5.2 + 0.6 * rng.random()
    vib = vibrato_cents * np.minimum(1.0, t / 0.6) * np.sin(2 * np.pi * rate * t + rng.uniform(0, 6.28))
    phase = 2 * np.pi * np.cumsum(f0 * 2.0 ** (vib / 1200.0)) / sr
    tone = np.zeros(b - a)
    k = 1
    while k * f0 < 9000 and k <= 40:
        g = k ** -1.1
        if formants:
            g *= sum(gain * np.exp(-0.5 * ((k * f0 - fc) / bw) ** 2) for fc, gain, bw in formants) + 0.02
        tone += g * np.sin(k * phase + rng.uniform(0, 6.28))
        k += 1
    if formants:
        breath = sosfilt(butter(2, [800, 6000], btype="band", fs=sr, output="sos"), rng.standard_normal(b - a))
        tone += 0.04 * breath * tone.std() / (breath.std() + 1e-12)
    env = np.minimum(1.0, t / 0.18) * np.minimum(1.0, (t[-1] - t) / 0.12 + 1e-9)
    out[a:b] = 0.1 * p.velocity * tone * env / (np.max(np.abs(tone)) + 1e-12)
    return out


VOWEL_AH = ((730.0, 1.0, 90.0), (1090.0, 0.5, 110.0), (2440.0, 0.25, 160.0))


def render_note(p: Played, total_s: float, sr: int = SR, seed: int = 1, pedal: bool = False) -> np.ndarray:
    if p.instrument == "piano":
        return piano_note(p, total_s, sr, seed, pedal)
    if p.instrument == "organ":
        return organ_note(p, total_s, sr, seed)
    if p.instrument == "guitar":
        return guitar_note(p, total_s, sr, seed)
    if p.instrument == "strings":
        return bowed_note(p, total_s, sr, seed)
    if p.instrument == "voice":
        return bowed_note(p, total_s, sr, seed, vibrato_cents=22.0, formants=VOWEL_AH)
    raise ValueError(f"unknown instrument {p.instrument}")
