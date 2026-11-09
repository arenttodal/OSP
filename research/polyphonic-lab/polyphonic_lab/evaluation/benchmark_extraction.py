"""P2 benchmark: every fixture through methods A/B/C, fed by ground-truth and by detected events,
scored against the isolated stems, with the ideal-ratio-mask oracle and the mixture as anchors."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from .. import corpus
from ..audio import read_audio
from ..config import environment, harmonic_config, load
from ..extraction.outputs import write_extraction
from ..extraction.separate import ExtractConfig, extract
from ..transcription import heuristic
from ..visualization.plots import bar_chart
from . import separation

VARIANTS = {
    "A": {"method": "A"},
    "B": {"method": "B"},
    "C": {"method": "C"},
}
SWEEP = {
    "B+smoothness": {"method": "B", "smoothness": True},
    "C/none": {"method": "C", "residual": "none"},
    "C/standard": {"method": "C", "residual": "standard"},
    "C/generous": {"method": "C", "residual": "generous"},
}
EXAMPLES = ("B-chord-C-major", "C-two-150ms", "B-chord-D-minor7", "E-pedal-arpeggio")
SUMMARY_KEYS = ("siSdrMedian", "siSdrImprovementMedian", "sirMedian", "sarMedian", "pitchErrorCentsMedianAbs",
                "attackEnergyErrorDbMedian", "envelopeCorrelationMedian", "logSpectralDistanceDbMedian")


def make_config(spec: dict) -> ExtractConfig:
    cfg = ExtractConfig(method=spec["method"], residual=spec.get("residual"))
    if spec.get("smoothness"):
        cfg.masks.smoothness = True
    return cfg


def run(index: Path, audio_root: Path, out: Path, events_sources: list[str], sweep: bool, config_path: str | None,
        only: list[str] | None = None, examples: bool = True) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    variants = dict(VARIANTS, **(SWEEP if sweep else {}))
    hc = harmonic_config(load(config_path))
    rows = []
    for mpath in corpus.manifest_paths(index):
        m = corpus.load_manifest(mpath)
        if only and m["id"] not in only:
            continue
        mix, sr = read_audio(audio_root / m["input"]["path"])
        x = mix[0]
        ref_events = corpus.truth_events(m)
        for e, n in zip(ref_events, m["groundTruth"]["notes"]):
            e.pitch_hz = n["pitch_hz"]
        refs = [read_audio(audio_root / n["stem"]["path"])[0][0] for n in m["groundTruth"]["notes"]]
        rt = m["groundTruth"].get("residualStem")
        residual_truth = read_audio(audio_root / rt["path"])[0][0] if rt else None
        base = {"fixture": m["id"], "class": m["class"], "family": m["family"], "notes": len(refs),
                "audioSeconds": m["input"]["durationSeconds"]}
        # anchors: the oracle (ideal ratio mask from the true stems) and the mixture itself
        irm = separation.oracle_irm(x, refs + ([residual_truth] if residual_truth is not None else []), sr)[:len(refs)]
        r = separation.evaluate_notes(x, sr, ref_events, refs, ref_events, irm)
        rows.append({**base, "events": "ground-truth", "variant": "oracle IRM", **{k: r[k] for k in SUMMARY_KEYS},
                     "missed": 0, "falseNotes": 0, "perNote": r["notes"]})
        sources = {}
        if "ground-truth" in events_sources:
            sources["ground-truth"] = ref_events
        if "harmonic" in events_sources:
            sources["harmonic"] = heuristic.detect(mix, sr, hc)
        for src, events in sources.items():
            for name, spec in variants.items():
                if src != "ground-truth" and name in SWEEP:
                    continue
                cfg = make_config(spec)
                ex = extract(mix, sr, events, cfg)
                est_events = [t.event for t in ex.templates]
                r = separation.evaluate_notes(x, sr, ref_events, refs, est_events, ex.notes, ex.residual, residual_truth)
                row = {**base, "events": src, "variant": name, **{k: r[k] for k in SUMMARY_KEYS},
                       "missed": r["missed"], "falseNotes": r["falseNotes"], "residualSiSdr": r.get("residualSiSdr"),
                       "consistencyMaxAbs": ex.consistency()["maxAbsError"], "masks": ex.info["masks"],
                       "timeRatio": ex.seconds / base["audioSeconds"], "perNote": r["notes"]}
                rows.append(row)
                if examples and m["id"] in EXAMPLES and name in VARIANTS:
                    write_extraction(ex, out / "examples" / m["id"] / f"{src}-{name}", cfg,
                                     meta={"input": m["input"]["path"], "fixture": m["id"], "events": src, "metrics": r})
        print(f"{m['id']}: " + "  ".join(f"{rw['variant']}[{rw['events'][:2]}] {rw['siSdrMedian']:.1f}"
                                         for rw in rows if rw["fixture"] == m["id"] and np.isfinite(rw["siSdrMedian"])))
    doc = {"schema": "osp-poly.benchmark-extraction/1", "environment": environment(), "rows": _clean(rows),
           "summary": _summary(rows)}
    (out / "metrics.json").write_text(json.dumps(doc, indent=1) + "\n")
    (out / "report.md").write_text(_report(doc))
    _charts(doc, out)
    return doc


def _clean(x):
    if isinstance(x, dict):
        return {k: _clean(v) for k, v in x.items()}
    if isinstance(x, list):
        return [_clean(v) for v in x]
    if isinstance(x, (float, np.floating)):
        return None if not np.isfinite(x) else float(f"{float(x):.6g}")
    if isinstance(x, (np.integer,)):
        return int(x)
    if isinstance(x, np.bool_):
        return bool(x)
    return x


def _summary(rows: list[dict]) -> list[dict]:
    out = []
    keys = sorted({(r["events"], r["variant"]) for r in rows}, key=lambda k: (k[0] != "ground-truth", k[1]))
    classes = sorted({r["class"] for r in rows}) + ["all"]
    for src, var in keys:
        for cls in classes:
            sel = [r for r in rows if r["events"] == src and r["variant"] == var and (cls == "all" or r["class"] == cls)]
            if not sel:
                continue
            notes = [n for r in sel for n in r["perNote"] if n.get("matched")]
            med = lambda k: float(np.nanmedian([n[k] for n in notes])) if notes else float("nan")
            out.append({"events": src, "variant": var, "class": cls, "fixtures": len(sel), "notes": len(notes),
                        "siSdr": med("siSdr"), "siSdrImprovement": separation._improvement(notes),
                        "sir": med("sir"), "sar": med("sar"),
                        "pitchErrorCentsAbs": float(np.nanmedian([abs(n["pitchErrorCents"]) for n in notes])) if notes else float("nan"),
                        "attackEnergyErrorDb": med("attackEnergyErrorDb"), "envelopeCorrelation": med("envelopeCorrelation"),
                        "logSpectralDistanceDb": med("logSpectralDistanceDb"),
                        "missed": int(sum(r.get("missed", 0) for r in sel)), "falseNotes": int(sum(r.get("falseNotes", 0) for r in sel)),
                        "consistencyMaxAbs": max((r.get("consistencyMaxAbs") or 0.0) for r in sel),
                        "timeRatio": float(np.mean([r["timeRatio"] for r in sel if "timeRatio" in r])) if any("timeRatio" in r for r in sel) else None})
    return _clean(out)


def _f(v, fmt="{:.1f}"):
    return "–" if v is None else fmt.format(v)


def _report(doc: dict) -> str:
    lines = ["# P2 extraction benchmark", "",
             "Measured against the corpus's isolated stems. Medians over notes. SIR/SAR are the projection",
             "(zero-lag) variant, not bss_eval. No listening is reflected here.", "",
             "| Events | Variant | Class | Notes | SI-SDR | Δ vs mix | SIR | SAR | Pitch |c| | Attack dB | Env r | LSD dB | Missed/false | Sum err | Time÷audio |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in doc["summary"]:
        lines.append(f"| {s['events']} | {s['variant']} | {s['class']} | {s['notes']} | {_f(s['siSdr'])} | {_f(s['siSdrImprovement'])} | "
                     f"{_f(s['sir'])} | {_f(s['sar'])} | {_f(s['pitchErrorCentsAbs'], '{:.2f}')} | {_f(s['attackEnergyErrorDb'])} | "
                     f"{_f(s['envelopeCorrelation'], '{:.2f}')} | {_f(s['logSpectralDistanceDb'])} | {s['missed']}/{s['falseNotes']} | "
                     f"{_f(s['consistencyMaxAbs'], '{:.1e}')} | {_f(s['timeRatio'], '{:.2f}')} |")
    lines += ["", "## Per fixture (SI-SDR median, dB)", ""]
    variants = []
    for r in doc["rows"]:
        k = f"{r['variant']} [{'gt' if r['events'] == 'ground-truth' else r['events']}]"
        if k not in variants:
            variants.append(k)
    lines.append("| Fixture | " + " | ".join(variants) + " |")
    lines.append("|---|" + "---|" * len(variants))
    for fid in dict.fromkeys(r["fixture"] for r in doc["rows"]):
        vals = {f"{r['variant']} [{'gt' if r['events'] == 'ground-truth' else r['events']}]": r["siSdrMedian"] for r in doc["rows"] if r["fixture"] == fid}
        lines.append(f"| {fid} | " + " | ".join(_f(vals.get(v)) for v in variants) + " |")
    return "\n".join(lines) + "\n"


def _charts(doc: dict, out: Path) -> None:
    classes = [c for c in ("A", "B", "C", "D", "E", "F", "all")]
    series = {}
    for var in ("oracle IRM", "A", "B", "C"):
        vals = []
        for cls in classes:
            s = next((s for s in doc["summary"] if s["events"] == "ground-truth" and s["variant"] == var and s["class"] == cls), None)
            vals.append(s["siSdr"] if s and s["siSdr"] is not None else 0.0)
        series[var] = vals
    bar_chart(classes, series, out / "si-sdr-by-class.png", "Median SI-SDR per class (ground-truth events)", "dB")
