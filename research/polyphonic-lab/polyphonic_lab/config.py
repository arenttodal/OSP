"""Analysis configurations (YAML) and the record of how a run was made."""

from __future__ import annotations

import dataclasses
import platform
import sys
from pathlib import Path

import yaml

from . import __version__
from .transcription.heuristic import HarmonicConfig

LAB = Path(__file__).resolve().parents[1]
DEFAULT = LAB / "configs" / "piano-baseline.yaml"


def load(path: str | Path | None) -> dict:
    p = Path(path) if path else DEFAULT
    return yaml.safe_load(p.read_text()) or {}


def harmonic_config(cfg: dict, **overrides) -> HarmonicConfig:
    h = HarmonicConfig()
    for k, v in ((cfg.get("detector") or {}).get("harmonic") or {}).items():
        if not hasattr(h, k):
            raise ValueError(f"unknown harmonic detector setting '{k}'")
        setattr(h, k, type(getattr(h, k))(v) if not isinstance(getattr(h, k), tuple) else tuple(v))
    for k, v in overrides.items():
        setattr(h, k, v)
    return h


def environment() -> dict:
    import numpy
    import scipy
    return {"lab": __version__, "python": sys.version.split()[0], "numpy": numpy.__version__, "scipy": scipy.__version__,
            "platform": platform.platform(), "machine": platform.machine(), "processor": platform.processor() or "unknown"}


def as_dict(obj) -> dict:
    d = dataclasses.asdict(obj)
    d.pop("extra", None)
    return d
