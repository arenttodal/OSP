#!/usr/bin/env python3
"""Media for the concept board: real editor captures (isolated popovers trimmed to their card and
shadow, full editors) and the mockup studio's shots, as WebP at two widths.
  prep_media.py <captures dir> <mockup shots dir> <out dir>"""
import os, sys
from PIL import Image
CAP, MOCK, OUT = sys.argv[1:4]
os.makedirs(OUT, exist_ok=True)

def save(im, name, widths):
    for w in widths:
        if im.width < w * 0.9 and w != widths[0]:
            continue
        r = im if im.width <= w else im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
        r.save(os.path.join(OUT, f"{name}-{w}.webp"), "WEBP", quality=88, method=6)

panels = {"life": "web-06-life", "drive": "web-06-macro-1", "filter": "web-06-macro-2", "movement": "web-06-macro-3",
          "space": "web-06-space", "echo": "web-09-echo", "advanced": "web-06-macro-5",
          "rm-kaleidoscope": "web-06-reimagined-1-kaleidoscope", "rm-tape-frame": "web-06-reimagined-2-tape-frame",
          "rm-toybox": "web-06-reimagined-3-toybox", "rm-mosaic": "web-06-reimagined-4-mosaic", "rm-mirage": "web-06-reimagined-5-mirage"}
for name, base in panels.items():
    im = Image.open(os.path.join(CAP, base + "-panel@3x.png")).convert("RGBA")
    l, t, r, b = im.getbbox()
    pad = 60  # keep the panel's soft shadow
    im = im.crop((max(0, l - pad), max(0, t - pad), min(im.width, r + pad), min(im.height, b + pad)))
    save(im, "panel-" + name, [640, 1100])
eds = {"editor-empty": "web-00-empty@2x", "editor-1L": "web-01-hero-1L@2x", "editor-2L": "web-01-hero-2L@2x", "editor-3L": "web-01-hero-3L@2x",
       "editor-granular": "web-09-granular-1L@2x", "editor-arp": "web-07-arp-3L@2x", "editor-keydna": "web-09-keydna@2x",
       "editor-capture": "web-09-capture@2x", "editor-notemap": "web-09-notemap@2x", "editor-synth": "web-03-synth-3-imprint@2x",
       "editor-library": "web-09-library@2x"}
for name, base in eds.items():
    p = os.path.join(CAP, base + ".png")
    if os.path.exists(p):
        save(Image.open(p).convert("RGB"), name, [1200, 2000])
for f in sorted(os.listdir(MOCK)):
    if f.endswith(".png") and not f.startswith("content-"):
        save(Image.open(os.path.join(MOCK, f)).convert("RGB"), "mock-" + f[:-4], [1200, 1920])
print("media:", len(os.listdir(OUT)), "files")
