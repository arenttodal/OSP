"""STFT / inverse STFT with exact reconstruction (scipy's ShortTimeFFT with its dual window).

A mask of ones reconstructs the input to numerical precision, and masks that sum to one
reconstruct the mixture as the sum of their parts (the separation's consistency check).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.signal import ShortTimeFFT
from scipy.signal.windows import hann


@dataclass(frozen=True)
class Stft:
    n_fft: int = 8192
    hop: int = 512
    sr: int = 48000

    def _engine(self) -> ShortTimeFFT:
        return ShortTimeFFT(hann(self.n_fft, sym=False), hop=self.hop, fs=self.sr, mfft=self.n_fft, scale_to=None)

    @property
    def bin_hz(self) -> float:
        return self.sr / self.n_fft

    def freqs(self) -> np.ndarray:
        return np.arange(self.n_fft // 2 + 1) * self.bin_hz

    def forward(self, x: np.ndarray) -> np.ndarray:
        """x: (frames,) -> complex (bins, time frames)."""
        return self._engine().stft(np.asarray(x, dtype=np.float64))

    def times(self, n_samples: int) -> np.ndarray:
        e = self._engine()
        return e.t(n_samples)

    def inverse(self, X: np.ndarray, n_samples: int) -> np.ndarray:
        e = self._engine()
        y = e.istft(X, k1=n_samples)
        return np.asarray(y[:n_samples], dtype=np.float64)

    def frame_of(self, seconds: float, n_samples: int) -> int:
        """The time frame whose centre is nearest `seconds`."""
        t = self.times(n_samples)
        return int(np.clip(np.searchsorted(t, seconds), 0, len(t) - 1))
