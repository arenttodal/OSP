#!/usr/bin/env python3
"""ANDOR Launch Asset Kit v2: assembles a kit folder from production outputs and writes its manifest.

  python3 build_kit.py --spec kit_v2_spec.json --out <kit dir> [--dry-run]

The spec lists each asset: where it comes from, its category, concept or shot ID, aspect,
status (final | draft | poc) and description. File names are generated, never hand-typed:

  andor-osp_<id>_<variant>_<aspect>_<width>w[_<fps>fps]_<status>_v<NN>.<ext>

so a draft or proof of concept can never carry a name implying final production quality.
Categories map to folders:

  01_editing-masters   full-quality masters (PNG sequences, ProRes): referenced by checksum and
                       production-storage path, not copied into the delivery kit
  02_web               lightweight website derivatives (H.264 / HEVC MP4, WebM, AVIF / WebP)
  03_paid              paid advertising compositions (4:5, 9:16, 1:1)
  04_feature-demos     feature demonstration footage (plugin captures, labelled by kind)
  05_posters           static posters and reduced-motion stills
  06_transparent       transparent product photography (RGBA)

IDs: the film concepts are FILM-D1 (instrument reveal), FILM-D2 (material montage) and FILM-D3
(dark campaign reveal); the cinematic shots are D01-D08 (the motion library). They are kept apart.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess

FOLDERS = {"master": "01_editing-masters", "web": "02_web", "paid": "03_paid", "feature": "04_feature-demos",
           "poster": "05_posters", "transparent": "06_transparent"}
STATUSES = ("final", "draft", "poc", "workflow-test")


def probe(path):
    """width, height, fps (None for stills), duration."""
    try:
        out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                              "stream=width,height,avg_frame_rate:format=duration", "-of", "json", path],
                             capture_output=True, text=True, check=True).stdout
        j = json.loads(out)
        st = j["streams"][0]
        num, den = (int(x) for x in st.get("avg_frame_rate", "0/1").split("/"))
        fps = round(num / den, 3) if den and num else None
        dur = float(j.get("format", {}).get("duration", 0) or 0)
        return st["width"], st["height"], (fps if dur > 0.2 else None), dur
    except Exception:
        from PIL import Image
        with Image.open(path) as im:
            return im.width, im.height, None, 0.0


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def name_for(a, w, fps):
    assert a["status"] in STATUSES, a
    parts = ["andor-osp", a["id"].lower(), a["variant"], a["aspect"].replace(":", "x"), f"{w}w"]
    if fps:
        parts.append(f"{int(round(fps))}fps")
    parts += [a["status"], "v%02d" % a.get("version", 1)]
    return "_".join(p for p in parts if p) + os.path.splitext(a["source"])[1].lower()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    spec = json.load(open(a.spec))
    base = os.path.dirname(os.path.abspath(a.spec))
    rows = []
    for asset in spec["assets"]:
        src = os.path.join(base, asset["source"]) if not os.path.isabs(asset["source"]) else asset["source"]
        if not os.path.exists(src):
            rows.append(dict(asset, error="missing source"))
            print("MISSING", asset["source"])
            continue
        if asset["category"] == "master":   # referenced, not copied
            rows.append(dict(asset, file=None, storage=asset.get("storage"), sha256=sha256(src) if os.path.isfile(src) else None))
            continue
        w, h, fps, dur = probe(src)
        fname = name_for(asset, w, fps)
        rel = os.path.join(FOLDERS[asset["category"]], fname)
        if not a.dry_run:
            os.makedirs(os.path.join(a.out, FOLDERS[asset["category"]]), exist_ok=True)
            shutil.copy2(src, os.path.join(a.out, rel))
        rows.append({"file": rel, "id": asset["id"], "concept": asset.get("concept"), "category": asset["category"],
                     "status": asset["status"], "aspect": asset["aspect"], "width": w, "height": h, "fps": fps,
                     "duration_s": round(dur, 3), "bytes": os.path.getsize(src), "sha256": sha256(src),
                     "description": asset.get("description", ""), "source": asset["source"],
                     "playback": asset.get("playback")})
    manifest = {"kit": spec["kit"], "version": spec["version"], "assets": rows,
                "naming": "andor-osp_<id>_<variant>_<aspect>_<width>w[_<fps>fps]_<status>_v<NN>.<ext>",
                "statuses": {"final": "final production quality", "draft": "draft quality for review, not for publication",
                             "poc": "proof of concept", "workflow-test": "synthetic test material, never flagship"}}
    if not a.dry_run:
        os.makedirs(a.out, exist_ok=True)
        json.dump(manifest, open(os.path.join(a.out, "asset_manifest.json"), "w"), indent=1)
        with open(os.path.join(a.out, "asset_manifest.csv"), "w") as f:
            cols = ["file", "id", "concept", "category", "status", "aspect", "width", "height", "fps", "duration_s", "bytes", "sha256", "description"]
            f.write(",".join(cols) + "\n")
            for r in rows:
                f.write(",".join('"%s"' % str(r.get(c, "") if r.get(c) is not None else "").replace('"', "'") for c in cols) + "\n")
    finals = sum(1 for r in rows if r.get("status") == "final")
    print(f"{len(rows)} assets ({finals} final, {len(rows) - finals} draft / poc / test)")


if __name__ == "__main__":
    main()
