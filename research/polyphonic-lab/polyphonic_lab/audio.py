"""Audio files, hashing and level helpers. Audio is float64, shape (channels, frames)."""

from __future__ import annotations

import hashlib
from pathlib import Path

import numpy as np
import soundfile as sf
from soundfile import _ffi, _snd

_SFC_SET_ADD_PEAK_CHUNK = 0x1050


def read_audio(path: str | Path) -> tuple[np.ndarray, int]:
    data, sr = sf.read(str(path), dtype="float64", always_2d=True)
    return np.ascontiguousarray(data.T), int(sr)


def write_audio(path: str | Path, audio: np.ndarray, sr: int) -> Path:
    """Lossless float32 WAV (no normalisation: levels are kept as they are)."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    x = np.atleast_2d(np.asarray(audio, dtype=np.float64))
    if not np.all(np.isfinite(x)):
        raise ValueError(f"refusing to write non-finite audio to {path}")
    with sf.SoundFile(str(path), "w", sr, x.shape[0], subtype="FLOAT", format="WAV") as f:
        # libsndfile adds a PEAK chunk with a timestamp to float WAVs: the same audio would hash
        # differently each second, and OSP identifies an atlas's sources by their files' bytes.
        _snd.sf_command(f._file, _SFC_SET_ADD_PEAK_CHUNK, _ffi.NULL, 0)
        f.write(x.T.astype(np.float32))
    return path


def sha256_file(path: str | Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def content_id(path: str | Path) -> str:
    """OSP's asset identity: "sha256:<hex>" of the file's bytes (io::sha256OfFile)."""
    return "sha256:" + sha256_file(path)


def mono(audio: np.ndarray) -> np.ndarray:
    x = np.atleast_2d(audio)
    return x.mean(axis=0)


def rms(x: np.ndarray) -> float:
    x = np.asarray(x, dtype=np.float64)
    return float(np.sqrt(np.mean(x * x))) if x.size else 0.0


def db(value: float, floor: float = 1e-12) -> float:
    return float(20.0 * np.log10(max(abs(value), floor)))


def stats(audio: np.ndarray, sr: int) -> dict:
    x = np.atleast_2d(audio)
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    r = rms(x)
    return {
        "frames": int(x.shape[1]),
        "channels": int(x.shape[0]),
        "seconds": round(x.shape[1] / sr, 6),
        "peakDbfs": round(db(peak), 3),
        "rmsDbfs": round(db(r), 3),
        "crestDb": round(db(peak) - db(r), 3) if r > 0 else None,
        "finite": bool(np.all(np.isfinite(x))),
    }


def fade(x: np.ndarray, sr: int, fade_in_s: float, fade_out_s: float) -> np.ndarray:
    """Short raised-cosine safety fades (in place on a copy)."""
    y = np.array(x, dtype=np.float64, copy=True)
    n = y.shape[-1]
    fi = min(n, int(round(fade_in_s * sr)))
    fo = min(n, int(round(fade_out_s * sr)))
    if fi > 1:
        y[..., :fi] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(fi) / fi)
    if fo > 1:
        y[..., n - fo:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fo) / fo)
    return y
