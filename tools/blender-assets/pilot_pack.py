"""Macro-knob pilot helpers (GUI asset upgrade, gate 1). Python 3 + Pillow; reuses postprocess.py.

  python3 pilot_pack.py comparison <render out dir> <dest dir>
      A stills-only comparison pack from a partial render (`generate.py --only ...`): every
      missing frame is filled with the nearest rendered one, so the real editor can be captured
      at the rendered angles. The manifest says so ("pilot": "stills-only"). Never ship it.

  python3 pilot_pack.py extract <pack.zip> <dest dir> <frame,frame,...>
      Frames of an existing runtime pack as PNGs (candidate A, the current production sprite).

Production packs are made only by `postprocess.py --stage frames` from a complete turn.
"""
import io
import json
import os
import sys
import zipfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import postprocess as pp  # noqa: E402


def comparison(render_out, dest):
    info = json.load(open(os.path.join(render_out, "render-info.json")))
    config = info["config"]
    count = info["frameCount"]
    have = {}
    for i in range(count):
        path = os.path.join(render_out, "master", f"frame_{i:03d}.png")
        if os.path.exists(path):
            have[i] = pp.prepare(Image.open(path).convert("RGBA"), config)
    if not have:
        raise SystemExit("no frames rendered")
    nearest = {i: min(have, key=lambda k: abs(k - i)) for i in range(count)}
    os.makedirs(dest, exist_ok=True)
    for side in config["runtime"]["levels"]:
        sized = {k: pp.resize(m, side) for k, m in have.items()}
        manifest = pp.manifest_for(config, info, side, count)
        manifest["pilot"] = f"stills-only: frames {sorted(have)} rendered, the rest copied from the nearest"
        pack = os.path.join(dest, f"{config['name']}-{side}.zip")
        with zipfile.ZipFile(pack, "w", compression=zipfile.ZIP_STORED) as z:
            def add(name, data):
                entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                entry.external_attr = 0o644 << 16
                z.writestr(entry, data)
            add("manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
            cache = {}
            for i in range(count):
                k = nearest[i]
                if k not in cache:
                    buf = io.BytesIO()
                    sized[k].save(buf, format="PNG", optimize=True)
                    cache[k] = buf.getvalue()
                add(f"frames/{i:03d}.png", cache[k])
        print("wrote", pack, f"({len(have)} rendered frames)")


def extract(pack, dest, frames):
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(pack) as z:
        for i in frames:
            data = z.read(f"frames/{i:03d}.png")
            open(os.path.join(dest, f"frame_{i:03d}.png"), "wb").write(data)
    print("extracted", len(frames), "frames from", pack)


if __name__ == "__main__":
    if sys.argv[1] == "comparison":
        comparison(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3], [int(x) for x in sys.argv[4].split(",")])
    else:
        raise SystemExit(__doc__)
