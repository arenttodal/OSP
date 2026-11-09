"""Plays an exported atlas through the real OSP engine (research-renderer --atlas) for listening.

The renders are made by OSP's own C++ engine C, not by the lab: the lab only writes standard MIDI
files (format 0, 480 ticks per quarter at 120 BPM) and calls the renderer.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
from pathlib import Path

from ..audio import read_audio, stats

REPO = Path(__file__).resolve().parents[4]
CANDIDATES = (REPO / "build" / "apps" / "research-renderer" / "research-renderer",
              REPO / "build-plugin" / "apps" / "research-renderer" / "research-renderer")
TPQ = 480
SECONDS_PER_TICK = 0.5 / TPQ        # 120 BPM


def renderer() -> Path | None:
    env = os.environ.get("OSP_RESEARCH_RENDERER")
    if env:
        return Path(env) if Path(env).exists() else None
    found = [p for p in CANDIDATES if p.exists()]
    if found:
        return max(found, key=lambda p: p.stat().st_mtime)
    w = shutil.which("research-renderer")
    return Path(w) if w else None


def _vlq(n: int) -> bytes:
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def write_midi(path: Path, notes: list[tuple[float, float, int, int]]) -> Path:
    """notes: (start_s, end_s, key, velocity). Format 0, one track, channel 1."""
    ev = []
    for start, end, key, vel in notes:
        ev.append((int(round(start / SECONDS_PER_TICK)), 1, 0x90, key, vel))
        ev.append((int(round(end / SECONDS_PER_TICK)), 0, 0x80, key, 0))
    ev.sort(key=lambda e: (e[0], e[1], e[3]))
    body, last = bytearray(), 0
    body += _vlq(0) + bytes([0xFF, 0x51, 0x03]) + (500000).to_bytes(3, "big")      # 120 BPM
    for tick, _, status, key, vel in ev:
        body += _vlq(tick - last) + bytes([status, key, vel])
        last = tick
    body += _vlq(0) + bytes([0xFF, 0x2F, 0x00])
    data = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ) + b"MTrk" + struct.pack(">I", len(body)) + bytes(body)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def demo_material(keys: list[int]) -> dict[str, list[tuple[float, float, int, int]]]:
    """What a first musical check plays: each recorded key alone, the recorded chord, a C-major
    scale, chords that were never recorded, and a short phrase."""
    out = {}
    out["recorded-keys"] = [(i * 1.6, i * 1.6 + 1.2, k, 96) for i, k in enumerate(keys)]
    out["recorded-chord"] = [(0.0, 2.5, k, 96) for k in keys]
    scale = [60, 62, 64, 65, 67, 69, 71, 72]
    out["c-major-scale"] = [(i * 0.5, i * 0.5 + 0.45, k, 90) for i, k in enumerate(scale)]
    chords = [(65, 69, 72), (57, 60, 64), (55, 59, 62), (60, 64, 67)]          # F, Am, G, C
    out["new-chords-F-Am-G-C"] = [(i * 1.5, i * 1.5 + 1.4, k, 92) for i, ch in enumerate(chords) for k in ch]
    phrase = [(0.0, 0.4, 64), (0.4, 0.8, 62), (0.8, 1.2, 60), (1.2, 1.6, 62), (1.6, 2.0, 64), (2.0, 2.4, 64), (2.4, 3.4, 64)]
    out["phrase"] = [(a, b, k, 88) for a, b, k in phrase] + [(0.0, 3.4, 48, 70)]
    return out


def render_demos(atlas: Path, out: Path, keys: list[int], mode: str = "oneshot") -> dict:
    exe = renderer()
    if exe is None:
        return {"rendered": False, "reason": "research-renderer not found (build the repo, or set OSP_RESEARCH_RENDERER)"}
    out = Path(out)
    results = {"rendered": True, "renderer": str(exe), "mode": mode, "renders": {}}
    for name, notes in demo_material(keys).items():
        mid = write_midi(out / "midi" / f"{name}.mid", notes)
        wav = out / f"{name}.wav"
        cmd = [str(exe), "--atlas", str(atlas), "--midi", str(mid), "--output", str(wav), "--mode", mode]
        p = subprocess.run(cmd, capture_output=True, text=True)
        entry = {"command": " ".join(cmd[1:]), "exit": p.returncode}
        if p.returncode == 0 and wav.exists():
            audio, sr = read_audio(wav)
            entry.update({"path": wav.name, **stats(audio, sr)})
        else:
            entry["stderr"] = p.stderr.strip()[-800:]
            results["rendered"] = False
        results["renders"][name] = entry
    return results


PLUGIN_TESTS = (REPO / "build-plugin" / "apps" / "plugin" / "osp_plugin_tests",)


def plugin_check(atlas: Path, out: Path) -> dict:
    """Loads the atlas into the real OSP plugin processor (headless), plays it, exports it as a
    portable instrument, imports that into a fresh sample store and plays it again (the hidden
    Catch2 test [.poly-lab-import] in tests/plugin/PolyLabTests.inc)."""
    import json
    exe = Path(os.environ["OSP_PLUGIN_TESTS"]) if os.environ.get("OSP_PLUGIN_TESTS") else next((p for p in PLUGIN_TESTS if p.exists()), None)
    if exe is None or not exe.exists():
        return {"ran": False, "reason": "osp_plugin_tests not built (cmake -B build-plugin -DOSP_BUILD_PLUGIN=ON; ninja), or set OSP_PLUGIN_TESTS"}
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OSP_POLY_ATLAS=str(Path(atlas).resolve()), OSP_POLY_OUT=str(out.resolve()))
    p = subprocess.run([str(exe), "[poly-lab-import]"], capture_output=True, text=True, env=env)
    res = {"ran": True, "exit": p.returncode, "passed": p.returncode == 0, "testBinary": str(exe)}
    summary = out / "plugin-import.json"
    if summary.exists():
        res["summary"] = json.loads(summary.read_text())
    if p.returncode != 0:
        res["output"] = (p.stdout + p.stderr)[-2000:]
    return res
