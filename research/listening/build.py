#!/usr/bin/env python3
"""Builds the blind listening lab page from one or more experiment runs.

    python3 research/listening/build.py <out-dir> <run-dir> [<run-dir> ...]

Each run directory comes from `research-renderer --experiment plan.json --output <run-dir>`.
Writes <out-dir>/index.html (template with every run's listening.json embedded, one tab per
run), <out-dir>/clips/* and <out-dir>/files.json (published-path -> source map). Only
listening.json is embedded: the page never sees key.json, or it stops being blind.

Clips are served as 320 kbps MP3 (ffmpeg + LAME): every browser plays it and artifact
hosting does not serve FLAC. The lossless renders stay in the run directories.
"""
import json
import pathlib
import shutil
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    out = pathlib.Path(sys.argv[1])
    runs = [pathlib.Path(p) for p in sys.argv[2:]]
    template = (pathlib.Path(__file__).parent / "template.html").read_text()
    marker = "/*OSP_DATA*/null"
    if marker not in template:
        print("template has no data marker", file=sys.stderr)
        return 1
    experiments = []
    files = {}
    (out / "clips").mkdir(parents=True, exist_ok=True)
    for run in runs:
        listening = json.loads((run / "listening.json").read_text())
        ext = listening.get("ext", "wav")
        for group in listening["groups"]:
            for clip in group["clips"]:
                name = f"clips/{clip}.mp3"
                target = out / name
                if not target.exists():
                    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(run / f"clips/{clip}.{ext}"),
                                    "-codec:a", "libmp3lame", "-b:a", "320k", str(target)], check=True)
                files[name] = name
        listening["ext"] = "mp3"
        experiments.append(listening)
    data = {"experiments": experiments}
    (out / "index.html").write_text(template.replace(marker, json.dumps(data)))
    (out / "files.json").write_text(json.dumps(dict(sorted(files.items()))))
    print(f"{len(experiments)} experiments, {len(files)} clips -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
