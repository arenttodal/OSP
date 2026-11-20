#!/usr/bin/env python3
"""Prepares a project's capture for Blender (system Python + Pillow).

  python3 vs_prep.py <project> --fps 24 [--master <textures dir>]

Crops captures/<fps>fps/face/NNNN.png to the master face texture's aspect (same width, from the
top), exactly as the master's osp_face.png was made, into captures/<fps>fps/face_crop/, and checks
that the capture's layout matches the master's (1448 px wide; heights within 4 px). A different
layout needs a new master face capture first (prep_textures.py) and is refused here.
"""
import argparse
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vstudio import project  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("project")
ap.add_argument("--fps", type=int, default=24)
ap.add_argument("--master", default=os.path.join(HERE, "..", "product-renders", "blender", "textures_0.9.0-beta.1"))
a = ap.parse_args()

cap = os.path.join(a.project, "captures", f"{a.fps}fps")
meta = json.load(open(os.path.join(cap, "capture_meta.json")))
geo = json.load(open(os.path.join(a.master, "geometry.json")))
mw, mh = geo["editor_px"]
cw, ch = meta["editor"]
if cw != mw or abs(ch - mh) > 4:
    sys.exit(f"capture layout {cw}x{ch} does not match the master's {mw}x{mh}: make a new master face capture first")
master = Image.open(os.path.join(a.master, "osp_face.png"))
out = os.path.join(cap, "face_crop")
os.makedirs(out, exist_ok=True)
n = meta["frames"]
bad = []
for i in range(1, n + 1):
    src = os.path.join(cap, "face", f"{i:04d}.png")
    dst = os.path.join(out, f"{i:04d}.png")
    if not project.png_ok(src):
        bad.append(i)
        continue
    if project.png_ok(dst):
        continue
    im = Image.open(src).convert("RGB")
    h = round(im.width * master.height / master.width)
    tmp = dst + ".tmp.png"
    im.crop((0, 0, im.width, h)).save(tmp)
    os.replace(tmp, dst)
print(f"{n - len(bad)} face frames cropped to the master's aspect -> {out}; capture {cw}x{ch} vs master {mw}x{mh}")
if bad:
    sys.exit(f"incomplete capture frames: {bad[:10]}{'...' if len(bad) > 10 else ''}")
