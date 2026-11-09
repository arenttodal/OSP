"""Spectrograms with note timelines (ground truth and detections) for reports."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

from ..audio import mono  # noqa: E402
from ..stft import Stft  # noqa: E402


def _quarter_tone(hz: float) -> tuple[float, float]:
    return hz * 2 ** (-1 / 24), hz * 2 ** (1 / 24)


def spectrogram_with_notes(audio: np.ndarray, sr: int, path: str | Path, title: str,
                           truth: list[dict] | None = None, detected: list[dict] | None = None,
                           fmin: float = 40.0, fmax: float = 8000.0) -> Path:
    """`truth` / `detected`: dicts with pitch_hz, onset, offset (and optional label)."""
    x = mono(audio)
    st = Stft(n_fft=4096, hop=256, sr=sr)
    S = np.abs(st.forward(x))
    t = st.times(len(x))
    f = st.freqs()
    keep = (f >= fmin) & (f <= fmax)
    Sdb = 20 * np.log10(S[keep] + 1e-9)
    Sdb -= Sdb.max()
    fig, ax = plt.subplots(figsize=(11, 4.6), dpi=110)
    ax.pcolormesh(t, f[keep], Sdb, shading="auto", cmap="magma", vmin=-80, vmax=0)
    ax.set_yscale("log")
    ax.set_ylim(fmin, fmax)
    ax.set_xlim(0, len(x) / sr)
    ax.set_xlabel("seconds")
    ax.set_ylabel("Hz")
    ax.set_title(title, fontsize=10)
    for notes, colour, lw in ((truth, "#7fe08a", 1.4), (detected, "#ff5a5a", 1.0)):
        for n in notes or []:
            lo, hi = _quarter_tone(n["pitch_hz"])
            ax.add_patch(Rectangle((n["onset"], lo), max(0.01, n["offset"] - n["onset"]), hi - lo,
                                   fill=False, edgecolor=colour, linewidth=lw))
            if n.get("label"):
                ax.text(n["onset"], hi * 1.01, n["label"], color=colour, fontsize=7, va="bottom")
    handles = []
    if truth:
        handles.append(plt.Line2D([], [], color="#7fe08a", label="ground truth"))
    if detected:
        handles.append(plt.Line2D([], [], color="#ff5a5a", label="detected"))
    if handles:
        ax.legend(handles=handles, loc="upper right", fontsize=8, framealpha=0.6)
    fig.tight_layout()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)
    return path


def bar_chart(labels: list[str], series: dict[str, list[float]], path: str | Path, title: str, ylabel: str) -> Path:
    fig, ax = plt.subplots(figsize=(max(6, 0.6 * len(labels) + 2), 3.6), dpi=110)
    width = 0.8 / max(1, len(series))
    xs = np.arange(len(labels))
    for i, (name, values) in enumerate(series.items()):
        ax.bar(xs + i * width - 0.4 + width / 2, values, width, label=name)
    ax.set_xticks(xs)
    ax.set_xticklabels(labels, rotation=30, ha="right", fontsize=8)
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontsize=10)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)
    return path
