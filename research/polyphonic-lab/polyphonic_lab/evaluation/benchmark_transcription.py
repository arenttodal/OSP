"""P1 benchmark: every fixture through each detector (and the harmonic detector's FFT/hop sweep)."""

from __future__ import annotations

import json
import time
from pathlib import Path

import numpy as np

from .. import corpus
from ..audio import read_audio
from ..config import as_dict, environment, harmonic_config, load
from ..transcription import basic_pitch_adapter, heuristic
from ..transcription.events import save_events
from ..visualization.plots import spectrogram_with_notes
from .transcription import evaluate

SWEEP = [(4096, 256), (4096, 512), (4096, 1024), (8192, 256), (8192, 512), (8192, 1024)]


def _plot_notes(events):
    return [{"pitch_hz": e.pitch_hz, "onset": e.onset_seconds, "offset": e.offset_seconds, "label": e.name} for e in events]


def run(index: Path, audio_root: Path, out: Path, methods: list[str], sweep: bool, config_path: str | None) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    cfg = load(config_path)
    variants: list[tuple[str, dict]] = []
    if "harmonic" in methods:
        variants.append(("harmonic", {}))
        if sweep:
            for n_fft, hop in SWEEP:
                variants.append((f"harmonic-{n_fft}-{hop}", {"n_fft": n_fft, "hop": hop}))
    if "basic-pitch" in methods:
        if basic_pitch_adapter.available():
            variants.append(("basic-pitch", {}))
        else:
            print("basic-pitch: not installed, skipped")
    results = {name: {} for name, _ in variants}
    meta = {"environment": environment(), "config": config_path or "configs/piano-baseline.yaml", "variants": {}}
    manifests = [corpus.load_manifest(p) for p in corpus.manifest_paths(index)]
    for m in manifests:
        audio, sr = read_audio(audio_root / m["input"]["path"])
        ref = corpus.truth_events(m)
        for name, over in variants:
            t0 = time.perf_counter()
            if name == "basic-pitch":
                est, info = basic_pitch_adapter.detect(audio_root / m["input"]["path"])
                meta["variants"][name] = info
            else:
                hc = harmonic_config(cfg, **over)
                est = heuristic.detect(audio, sr, hc)
                meta["variants"][name] = {"detector": "harmonic", "settings": as_dict(hc)}
            elapsed = time.perf_counter() - t0
            r = evaluate(ref, est)
            r["seconds"] = round(elapsed, 3)
            r["audioSeconds"] = m["input"]["durationSeconds"]
            results[name][m["id"]] = r
            if name in ("harmonic", "basic-pitch"):
                save_events(out / "notes" / name / f"{m['id']}.json", est, {"fixture": m["id"], "detector": name})
                spectrogram_with_notes(audio, sr, out / "images" / name / f"{m['id']}.png",
                                       f"{m['id']} - {name}: F1 {r['f1']:.2f} (onset), {r['f1WithOffsets']:.2f} (with offsets)",
                                       truth=[{"pitch_hz": n["pitch_hz"], "onset": n["onset_seconds"], "offset": n["offset_seconds"]} for n in m["groundTruth"]["notes"]],
                                       detected=_plot_notes(est))
        print(f"{m['id']}: " + ", ".join(f"{n} {results[n][m['id']]['f1']:.2f}" for n, _ in variants))
    # Stability: the baseline twice on every fixture, identical events.
    stable = True
    if "harmonic" in methods:
        hc = harmonic_config(cfg)
        for m in manifests:
            audio, sr = read_audio(audio_root / m["input"]["path"])
            a = [(e.pitch_midi, e.onset_seconds, e.offset_seconds) for e in heuristic.detect(audio, sr, hc)]
            b = [(e.pitch_midi, e.onset_seconds, e.offset_seconds) for e in heuristic.detect(audio, sr, hc)]
            stable = stable and a == b
    summary = _summarise(results, manifests)
    doc = {"schema": "osp-poly.transcription-benchmark/1", "meta": meta, "stableAcrossRepeats": stable, "summary": summary, "results": results}
    (out / "metrics.json").write_text(json.dumps(doc, indent=1) + "\n")
    (out / "report.md").write_text(_report(doc, manifests))
    return doc


def _summarise(results: dict, manifests: list[dict]) -> dict:
    classes = sorted({m["class"] for m in manifests})
    out = {}
    for name, per in results.items():
        row = {}
        for c in classes + ["all", "controlled-chords"]:
            ids = [m["id"] for m in manifests if (c == "all" or m["class"] == c or (c == "controlled-chords" and m["class"] == "B"))]
            rs = [per[i] for i in ids if i in per]
            if not rs:
                continue
            on = [r["onsetErrorMedianMs"] for r in rs if r["onsetErrorMedianMs"] is not None]
            row[c] = {"fixtures": len(rs), "meanF1": round(float(np.mean([r["f1"] for r in rs])), 3),
                      "meanF1WithOffsets": round(float(np.mean([r["f1WithOffsets"] for r in rs])), 3),
                      "medianOnsetErrorMs": round(float(np.median(on)), 1) if on else None,
                      "octaveErrors": int(sum(r["octaveErrors"] for r in rs)), "missed": int(sum(r["missed"] for r in rs)),
                      "falseNotes": int(sum(r["falseNotes"] for r in rs)),
                      "realTimeFactor": round(float(sum(r["seconds"] for r in rs) / sum(r["audioSeconds"] for r in rs)), 3)}
        out[name] = row
    return out


def _report(doc: dict, manifests: list[dict]) -> str:
    s = doc["summary"]
    lines = ["# P1 transcription benchmark", "",
             "Tolerances: onset 50 ms, pitch 50 cents; offsets within max(50 ms, 20 % of the note) of the key release.",
             f"Stable across repeated analysis: **{'yes' if doc['stableAcrossRepeats'] else 'NO'}**.", "",
             "## By class (mean note F1, onset only / with offsets; median onset error; octave errors; missed / false; time / audio time)", "",
             "| Detector | Class | Fixtures | F1 | F1 + offsets | Onset error ms | Octave err. | Missed | False | RT factor |", "|---|---|---|---|---|---|---|---|---|---|"]
    for name, row in s.items():
        for c, r in row.items():
            lines.append(f"| {name} | {c} | {r['fixtures']} | {r['meanF1']} | {r['meanF1WithOffsets']} | {r['medianOnsetErrorMs']} | "
                         f"{r['octaveErrors']} | {r['missed']} | {r['falseNotes']} | {r['realTimeFactor']} |")
    lines += ["", "## Per fixture (note F1, onset only)", "", "| Fixture | " + " | ".join(s.keys()) + " |", "|---|" + "---|" * len(s)]
    for m in manifests:
        lines.append(f"| {m['id']} | " + " | ".join(f"{doc['results'][n][m['id']]['f1']:.2f}" for n in s) + " |")
    lines += ["", "Images: `images/<detector>/<fixture>.png` (green: ground truth to the key release; red: detected)."]
    return "\n".join(lines) + "\n"
