#!/usr/bin/env python3
"""Cuts the instrument and its floor shadow out of each cine_rig frame, as RGBA WebP, so a page can
lay it over a separately moving backdrop (parallax, drifting light).
  compose_rgba.py <beauty dir> <matte dir> <clean.png> <out dir> [--width 1600] [--quality 86]
Per pixel: the product where the matte says so; elsewhere black at the shadow's density, taken from
the beauty / clean ratio (the backdrop alone, rendered once: the camera does not move)."""
import os, sys
import numpy as np
from PIL import Image
B, M, C, OUT = sys.argv[1:5]
opt = lambda k, d: type(d)(sys.argv[sys.argv.index(k) + 1]) if k in sys.argv else d
W, Q = opt("--width", 1600), opt("--quality", 86)
os.makedirs(OUT, exist_ok=True)
pngs = lambda d: sorted(f for f in os.listdir(d) if f.endswith(".png"))
clean = np.asarray(Image.open(C).convert("RGB"), dtype=np.float32) / 255.0
cl = clean.mean(2, keepdims=True)
for i, (fb, fm) in enumerate(zip(pngs(B), pngs(M))):
    b = np.asarray(Image.open(os.path.join(B, fb)).convert("RGB"), dtype=np.float32) / 255.0
    mi = Image.open(os.path.join(M, fm))
    m = (np.asarray(mi.split()[-1] if mi.mode in ("RGBA", "LA") else mi.convert("L"), dtype=np.float32) / 255.0)[..., None]
    ratio = np.clip(b.mean(2, keepdims=True) / np.maximum(cl, 1e-3), 0.0, 1.0)
    shadow = (1.0 - ratio) * (1.0 - m)                  # floor darkening outside the product
    a = np.clip(m + shadow, 0.0, 1.0)
    rgb = np.where(a > 1e-4, (m * b) / np.maximum(a, 1e-4), 0.0)
    im = Image.fromarray((np.clip(np.concatenate([rgb, a], 2), 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
    if im.width > W:
        im = im.resize((W, round(im.height * W / im.width)), Image.LANCZOS)
    im.save(os.path.join(OUT, f"rise-{i:02d}.webp"), "WEBP", quality=Q, method=6)
Image.open(C).convert("RGB").resize((W, round(clean.shape[0] * W / clean.shape[1])), Image.LANCZOS).save(os.path.join(OUT, "rise-backdrop.webp"), "WEBP", quality=90)
print("frames:", i + 1)
