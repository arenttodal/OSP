#!/usr/bin/env python3
"""Builds the blind listening page for a rendered bake-off run.

    python3 research/bakeoff/listening-page/build.py research/bakeoff/pitch-bakeoff-1 out/

Writes out/index.html (template with listening.json embedded), out/clips/*.wav and
out/files.json (published-path -> source map for hosting the clips next to the page).
Only listening.json is embedded: the page must never see key.json, or it stops being blind.
FAMILY_INFO in the template names the plan-1 sources; edit it for other plans.
"""
import json
import pathlib
import shutil
import sys

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2
    run, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    listening = json.loads((run / "listening.json").read_text())
    template = (pathlib.Path(__file__).parent / "template.html").read_text()
    marker = "/*OSP_DATA*/null"
    if marker not in template:
        print("template has no data marker", file=sys.stderr)
        return 1
    page = template.replace(marker, json.dumps(listening))
    page = page.replace('const RUN = "pitch-bakeoff-1";', f'const RUN = {json.dumps(listening.get("name", run.name))};')
    (out / "clips").mkdir(parents=True, exist_ok=True)
    files = {}
    for group in listening["groups"]:
        for clip in group["clips"]:
            name = f"clips/{clip}.wav"
            shutil.copyfile(run / name, out / name)
            files[name] = name
    (out / "index.html").write_text(page)
    (out / "files.json").write_text(json.dumps(dict(sorted(files.items()))))
    print(f"{len(files)} clips -> {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
