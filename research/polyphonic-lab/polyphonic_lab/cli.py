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

    args = p.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, ValueError, KeyError, RuntimeError) as e:
        print(f"osp-poly: error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
