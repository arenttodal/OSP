#!/usr/bin/env python3
"""Puts the H01 instrument (cine_rig beauty, matte and clean passes) in front of a new backdrop.
  composite_hero.py <beauty.png> <matte.png> <clean.png> <backdrop.png> <out.png> [--shadow 1.0]
The backdrop takes the floor's shading from the passes (shadow ratio = beauty / clean, outside the
product), so the instrument keeps its contact shadow and reflection on any background."""
import sys
import numpy as np
from PIL import Image
b, m, c, bg, out = sys.argv[1:6]
k = float(sys.argv[sys.argv.index("--shadow") + 1]) if "--shadow" in sys.argv else 1.0
load = lambda p, mode="RGB": np.asarray(Image.open(p).convert(mode), dtype=np.float32) / 255.0
beauty, clean = load(b), load(c)
mi = Image.open(m); alpha = (np.asarray(mi.split()[-1] if mi.mode in ("RGBA", "LA") else mi.convert("L"), dtype=np.float32) / 255.0)[..., None]
back = np.asarray(Image.open(bg).convert("RGB").resize((beauty.shape[1], beauty.shape[0]), Image.LANCZOS), dtype=np.float32) / 255.0
ratio = np.clip(beauty.mean(2, keepdims=True) / np.maximum(clean.mean(2, keepdims=True), 1e-3), 0.0, 1.0)
ratio = 1.0 - (1.0 - ratio) * k
res = alpha * beauty + (1.0 - alpha) * back * ratio
Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)).save(out)
print("wrote", out)
