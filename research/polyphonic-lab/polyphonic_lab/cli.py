"""osp-poly: the lab's command line. Every command returns nonzero on failure."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .corpus import MANIFESTS, OUTPUT


def _corpus(args) -> int:
    from . import corpus
    if args.action == "generate":
        written = corpus.generate(Path(args.audio_root), Path(args.manifests), args.only or None)
        print(f"generated {len(written)} fixtures under {args.audio_root}/corpus; manifests in {args.manifests}")
        return 0 if written else 1
    ok, summary = corpus.validate(Path(args.manifest), Path(args.audio_root), Path(args.out) if args.out else None, plots=not args.no_plots)
    for r in summary["fixtures"]:
        flag = "ok " if not r["errors"] else "ERR"
        print(f"{flag} {r['id']}" + ("".join(f"\n    error: {e}" for e in r["errors"])) + ("".join(f"\n    warning: {w}" for w in r["warnings"])))
    print("corpus valid" if ok else "corpus INVALID")
    return 0 if ok else 2


def _detect(args) -> int:
    from .audio import read_audio
    from .config import as_dict, environment, harmonic_config, load
    from .evaluation.transcription import evaluate
    from .transcription import basic_pitch_adapter, ground_truth, heuristic
    from .transcription.events import save_events
    from .visualization.plots import spectrogram_with_notes
    import time
    out = Path(args.out)
    audio, sr = read_audio(args.input)
    t0 = time.perf_counter()
    if args.method == "ground-truth":
        if not args.manifest:
            raise ValueError("--method ground-truth needs --manifest")
        events, info = ground_truth.detect(args.manifest)
    elif args.method == "basic-pitch":
        events, info = basic_pitch_adapter.detect(args.input)
    else:
        hc = harmonic_config(load(args.config))
        events = heuristic.detect(audio, sr, hc)
        info = {"detector": "harmonic", "settings": as_dict(hc)}
    elapsed = time.perf_counter() - t0
    save_events(out / "notes.json", events, {"input": str(args.input), **info})
    run = {"input": str(args.input), "method": args.method, "seconds": round(elapsed, 3), "notes": len(events), "environment": environment(), **info}
    truth = None
    if args.manifest:
        from . import corpus
        m = corpus.load_manifest(Path(args.manifest))
        truth = [{"pitch_hz": n["pitch_hz"], "onset": n["onset_seconds"], "offset": n["offset_seconds"]} for n in m["groundTruth"]["notes"]]
        run["metrics"] = evaluate(corpus.truth_events(m), events)
    (out / "run.json").write_text(json.dumps(run, indent=1) + "\n")
    spectrogram_with_notes(audio, sr, out / "detected-notes.png", f"{Path(args.input).name}: {args.method}", truth=truth,
                           detected=[{"pitch_hz": e.pitch_hz, "onset": e.onset_seconds, "offset": e.offset_seconds, "label": e.name} for e in events])
    print(f"{len(events)} notes: " + " ".join(f"{e.name}@{e.onset_seconds:.3f}" for e in events[:24]) + (" ..." if len(events) > 24 else ""))
    if "metrics" in run:
        r = run["metrics"]
        print(f"F1 {r['f1']} (with offsets {r['f1WithOffsets']}), missed {r['missed']}, false {r['falseNotes']}, octave errors {r['octaveErrors']}")
    print(f"wrote {out}")
    return 0


def _extract(args) -> int:
    from .audio import read_audio
    from .extraction.outputs import write_extraction
    from .extraction.separate import ExtractConfig, extract
    from .transcription.events import load_events
    audio, sr = read_audio(args.input)
    events = load_events(args.notes)
    cfg = ExtractConfig(method=args.method, residual=args.residual)
    cfg.masks.smoothness = args.smoothness
    ex = extract(audio, sr, events, cfg)
    meta = {"input": str(args.input), "events": str(args.notes)}
    if args.manifest:
        from . import corpus
        from .evaluation import separation
        m = corpus.load_manifest(Path(args.manifest))
        root = Path(args.audio_root)
        refs = [read_audio(root / n["stem"]["path"])[0][0] for n in m["groundTruth"]["notes"]]
        rt = m["groundTruth"].get("residualStem")
        meta["metrics"] = separation.evaluate_notes(ex.mixture, sr, corpus.truth_events(m), refs, [t.event for t in ex.templates],
                                                    ex.notes, ex.residual, read_audio(root / rt["path"])[0][0] if rt else None)
    doc = write_extraction(ex, Path(args.out), cfg, meta, plots=not args.no_plots)
    c = doc["consistency"]
    print(f"method {ex.method} ({ex.residual_strategy} residual): {len(ex.notes)} notes in {ex.seconds:.2f} s; "
          f"notes + residual = mixture within {c['maxAbsError']:.1e}")
    for n in doc["notes"]:
        print(f"  {n['name']:4s} f0 {n['f0_hz']:.2f} Hz ({n['tuning_cents']:+.1f} c), sample {n['sample']['start_seconds']:.3f}-"
              f"{n['sample']['end_seconds']:.3f} s, peak {n['sample']['peak_dbfs']:.1f} dBFS -> {n['sample']['path']}")
    if "metrics" in meta:
        r = meta["metrics"]
        print(f"SI-SDR median {r['siSdrMedian']:.1f} dB (vs mixture +{r['siSdrImprovementMedian']:.1f}), "
              f"SIR {r['sirMedian']:.1f}, SAR {r['sarMedian']:.1f}, missed {r['missed']}, false {r['falseNotes']}")
    print(f"wrote {args.out}")
    return 0


def _export_atlas(args) -> int:
    from .mapping.atlas import export_atlas
    from .mapping.curate import CurateConfig
    r = export_atlas(Path(args.extraction), Path(args.out), args.name, args.mapping, args.include, not args.no_fill, args.max_transpose,
                     CurateConfig(spacing=args.spacing, max_notes=args.max_notes))
    print(f"wrote {r['atlas']}: {r['included']} of {r['notes']} notes included, keys {r['keys']}, "
          f"each reaching {r['maxTransposeSemitones']:g} semitones")
    code = 0
    if args.render:
        from .mapping.render import render_demos
        rr = render_demos(Path(r["atlas"]), Path(args.out) / "renders", r["keys"], args.mode)
        _print_renders(rr)
        (Path(args.out) / "renders.json").write_text(json.dumps(rr, indent=1) + "\n")
        code = code or (0 if rr["rendered"] else 4)
    if args.plugin_check:
        from .mapping.render import plugin_check
        pc = plugin_check(Path(r["atlas"]), Path(args.out) / "plugin")
        (Path(args.out) / "plugin-check.json").write_text(json.dumps(pc, indent=1) + "\n")
        if not pc["ran"]:
            print(f"plugin check: skipped ({pc['reason']})")
        else:
            s = pc.get("summary", {})
            print(f"plugin check: {'passed' if pc['passed'] else 'FAILED'} - {s.get('members')} notes loaded, "
                  f"missing sources {s.get('missingSources')}, re-imported identical {s.get('reimportedIdentical')}")
            code = code or (0 if pc["passed"] else 5)
    return code


def _print_renders(rr: dict) -> None:
    if not rr.get("renders"):
        print(f"renders: skipped ({rr.get('reason')})")
        return
    for name, e in rr["renders"].items():
        print(f"  render {name}: " + (f"{e['seconds']:.2f} s, peak {e['peakDbfs']:.1f} dBFS" if e["exit"] == 0 else f"FAILED ({e.get('stderr', '')})"))


def _build(args) -> int:
    """The whole first-milestone pipeline: recording -> notes -> per-note audio -> OSP atlas -> renders."""
    import subprocess
    out = Path(args.out)
    detect_args = [args.input, "--method", args.detector, "--out", str(out / "detect")]
    if args.manifest:
        detect_args += ["--manifest", args.manifest]
    if args.config:
        detect_args += ["--config", args.config]
    steps = [("detect", ["detect", *detect_args]),
             ("extract", ["extract", args.input, "--notes", str(out / "detect" / "notes.json"), "--method", args.method,
                          "--out", str(out / "extraction")] + (["--manifest", args.manifest] if args.manifest else [])
                         + (["--residual", args.residual] if args.residual else [])),
             ("export-atlas", ["export-atlas", str(out / "extraction"), "--out", str(out / "instrument"), "--name", args.name,
                               "--mapping", args.mapping, "--include", args.include] + (["--render", "--mode", args.mode] if args.render else [])
                              + (["--plugin-check"] if args.plugin_check else []))]
    for name, argv in steps:
        print(f"== {name}")
        code = main(argv)
        if code != 0:
            print(f"osp-poly: build stopped at {name} (exit {code}); outputs so far are kept in {out}", file=sys.stderr)
            return code
    print(f"instrument: {out / 'instrument' / (args.name + '.andoratlas')}")
    return 0


def _benchmark(args) -> int:
    out = Path(args.out)
    if args.stage == "extraction":
        from .evaluation import benchmark_extraction
        benchmark_extraction.run(Path(args.manifest), Path(args.audio_root), out, args.events, args.sweep, args.config, args.only)
        print(f"wrote {out}/report.md")
        return 0
    if args.stage == "transcription":
        from .evaluation import benchmark_transcription
        doc = benchmark_transcription.run(Path(args.manifest), Path(args.audio_root), out, args.methods, args.sweep, args.config)
        print(f"wrote {out}/report.md")
        return 0 if doc["stableAcrossRepeats"] else 3
    raise ValueError(f"unknown benchmark stage {args.stage}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="osp-poly", description="ANDOR OSP Polyphonic Instrument Extraction Lab (research)")
    p.add_argument("--version", action="version", version=__version__)
    sub = p.add_subparsers(dest="command", required=True)

    c = sub.add_parser("corpus", help="generate or validate the reference corpus")
    c.add_argument("action", choices=["generate", "validate"])
    c.add_argument("--manifest", default=str(MANIFESTS / "corpus.json"), help="corpus index or one fixture manifest")
    c.add_argument("--manifests", default=str(MANIFESTS), help="(generate) where manifests are written")
    c.add_argument("--audio-root", default=str(OUTPUT), help="where the corpus audio lives (corpus/<id>/...)")
    c.add_argument("--out", help="(validate) report folder (default <audio-root>/corpus-report)")
    c.add_argument("--only", nargs="*", help="(generate) fixture ids")
    c.add_argument("--no-plots", action="store_true")
    c.set_defaults(func=_corpus)

    d = sub.add_parser("detect", help="detect simultaneous notes (writes notes.json, run.json, detected-notes.png)")
    d.add_argument("input")
    d.add_argument("--method", choices=["harmonic", "basic-pitch", "ground-truth"], default="harmonic")
    d.add_argument("--manifest", help="a fixture manifest: ground truth for --method ground-truth, metrics otherwise")
    d.add_argument("--config", help="YAML configuration (default configs/piano-baseline.yaml)")
    d.add_argument("--out", required=True)
    d.set_defaults(func=_detect)

    e = sub.add_parser("extract", help="separate each note from the mixture (writes notes/, full/, residual, extraction.json)")
    e.add_argument("input")
    e.add_argument("--notes", required=True, help="note events (osp-poly.notes/1, from detect)")
    e.add_argument("--method", choices=["A", "B", "C"], default="C", help="A simple harmonic masks, B harmonic NMF, C B + attacks")
    e.add_argument("--residual", choices=["none", "conservative", "standard", "generous"], help="default: the method's own")
    e.add_argument("--smoothness", action="store_true", help="(B, C) re-divide colliding partials by spectral smoothness")
    e.add_argument("--manifest", help="a fixture manifest: score against its isolated stems")
    e.add_argument("--audio-root", default=str(OUTPUT))
    e.add_argument("--no-plots", action="store_true")
    e.add_argument("--out", required=True)
    e.set_defaults(func=_extract)

    a = sub.add_parser("export-atlas", help="an extraction -> an OSP Note Atlas (+ samples, sidecar), optionally rendered by OSP")
    a.add_argument("extraction", help="an extract output folder (extraction.json)")
    a.add_argument("--out", required=True)
    a.add_argument("--name", default="instrument")
    a.add_argument("--mapping", choices=["original", "compact"], default="original")
    a.add_argument("--include", choices=["curated", "best", "all"], default="curated",
                   help="curated: the best notes, spread out and stretched over the keyboard; best: one take per key; all: every take")
    a.add_argument("--spacing", type=int, default=3, help="(curated) semitones between kept notes")
    a.add_argument("--max-notes", type=int, default=12, help="(curated) at most this many notes")
    a.add_argument("--no-fill", action="store_true", help="(original) keys without a note stay silent")
    a.add_argument("--max-transpose", type=float, default=None, help="FILL reach (default: curated covers A0-C8, others 12)")
    a.add_argument("--render", action="store_true", help="play demo MIDI through OSP's research-renderer")
    a.add_argument("--mode", choices=["oneshot", "granular"], default="oneshot")
    a.add_argument("--plugin-check", action="store_true", help="load, play, export and re-import it in the OSP plugin (osp_plugin_tests)")
    a.set_defaults(func=_export_atlas)

    bu = sub.add_parser("build", help="recording -> detect -> extract -> export-atlas (-> OSP renders)")
    bu.add_argument("input")
    bu.add_argument("--out", required=True)
    bu.add_argument("--detector", choices=["harmonic", "basic-pitch", "ground-truth"], default="harmonic")
    bu.add_argument("--manifest", help="fixture manifest (ground truth / metrics)")
    bu.add_argument("--config")
    bu.add_argument("--method", choices=["A", "B", "C"], default="C")
    bu.add_argument("--residual", choices=["none", "conservative", "standard", "generous"])
    bu.add_argument("--name", default="instrument")
    bu.add_argument("--mapping", choices=["original", "compact"], default="original")
    bu.add_argument("--include", choices=["curated", "best", "all"], default="curated")
    bu.add_argument("--render", action="store_true")
    bu.add_argument("--mode", choices=["oneshot", "granular"], default="oneshot")
    bu.add_argument("--plugin-check", action="store_true")
    bu.set_defaults(func=_build)

    b = sub.add_parser("benchmark", help="compare methods over the corpus")
    b.add_argument("stage", choices=["transcription", "extraction"])
    b.add_argument("--events", nargs="+", default=["ground-truth", "harmonic"], help="(extraction) where the note events come from")
    b.add_argument("--only", nargs="*", help="(extraction) fixture ids")
    b.add_argument("--manifest", default=str(MANIFESTS / "corpus.json"))
    b.add_argument("--audio-root", default=str(OUTPUT))
    b.add_argument("--methods", nargs="+", default=["harmonic", "basic-pitch"])
    b.add_argument("--sweep", action="store_true", help="(transcription) the detector at FFT 4096/8192 x hop 256/512/1024; (extraction) residual strategies and smoothness")
    b.add_argument("--config")
    b.add_argument("--out", required=True)
    b.set_defaults(func=_benchmark)

    args = p.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, ValueError, KeyError, RuntimeError) as e:
        print(f"osp-poly: error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
