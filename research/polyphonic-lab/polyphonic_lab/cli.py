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


def _benchmark(args) -> int:
    out = Path(args.out)
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

    b = sub.add_parser("benchmark", help="compare methods over the corpus")
    b.add_argument("stage", choices=["transcription"])
    b.add_argument("--manifest", default=str(MANIFESTS / "corpus.json"))
    b.add_argument("--audio-root", default=str(OUTPUT))
    b.add_argument("--methods", nargs="+", default=["harmonic", "basic-pitch"])
    b.add_argument("--sweep", action="store_true", help="(transcription) also the harmonic detector at FFT 4096/8192 x hop 256/512/1024")
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
