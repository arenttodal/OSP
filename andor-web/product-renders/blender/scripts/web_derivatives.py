"""Gate C3 web derivatives (Pillow 12+, AVIF and WebP) from the archival PNG masters.

  python3 product-renders/blender/scripts/web_derivatives.py <masters dir> <out dir>

For every master: AVIF and WebP at the responsive widths (never upscaled). Transparent masters keep
their alpha; each is also checked over light, mid and dark backgrounds (alpha_check_*.jpg) so
fringes show. Writes derivatives.json and derivatives.md: dimensions, settings and real sizes.
Typography is never added: it stays live in Framer.
"""
import json
import os
import sys

from PIL import Image

src, out = sys.argv[1:3]
only = sys.argv[3:]  # optional master names: process just these (keeps memory low; rows still cover every master)
os.makedirs(out, exist_ok=True)
WIDTHS = [640, 960, 1280, 1920, 2560, 3840]
WEBP = {"quality": 86, "method": 6}
AVIF = {"quality": 62, "speed": 4}
BACKS = {"light": (247, 246, 242), "mid": (150, 146, 140), "dark": (24, 22, 20)}
rows = []

for name in sorted(os.listdir(src)):
    if not name.endswith(".png"):
        continue
    im = Image.open(os.path.join(src, name))
    alpha = im.mode in ("RGBA", "LA")
    base = os.path.splitext(name)[0]
    work = not only or name in only or base in only
    if work:
        im = im.convert("RGBA" if alpha else "RGB")
    widths = [w for w in WIDTHS if w < im.width] + [im.width]
    for w in sorted(set(widths)):
        h = round(im.height * w / im.width)
        r = None
        for fmt, opts in (("webp", WEBP), ("avif", AVIF)):
            path = os.path.join(out, f"{base}-{w}.{fmt}")
            # Resumable: a file already written is kept (an interrupted run is rerun per master).
            if work and not os.path.exists(path):
                if r is None:
                    r = im if w == im.width else im.resize((w, h), Image.LANCZOS)
                r.save(path, fmt.upper(), **opts)
            if not os.path.exists(path):
                continue
            rows.append({"master": name, "format": fmt, "width": w, "height": h, "alpha": alpha,
                         "settings": opts, "bytes": os.path.getsize(path), "file": os.path.basename(path)})
    if alpha and work and not os.path.exists(os.path.join(out, f"alpha_check_{base}.jpg")):
        tile = im.resize((1400, round(im.height * 1400 / im.width)), Image.LANCZOS)
        sheet = Image.new("RGB", (tile.width * 3, tile.height))
        for i, rgb in enumerate(BACKS.values()):
            bg = Image.new("RGBA", tile.size, rgb + (255,))
            bg.alpha_composite(tile)
            sheet.paste(bg.convert("RGB"), (i * tile.width, 0))
        sheet.save(os.path.join(out, f"alpha_check_{base}.jpg"), quality=92)

json.dump(rows, open(os.path.join(out, "derivatives.json"), "w"), indent=1)
lines = ["# Web derivatives", "", f"WebP {WEBP} · AVIF {AVIF} · widths {WIDTHS} (never upscaled)", "",
         "| Master | Format | Size (px) | Alpha | File size |", "|---|---|---|---|---|"]
for r in rows:
    lines.append(f"| {r['master']} | {r['format']} | {r['width']} x {r['height']} | {'yes' if r['alpha'] else 'no'} | {r['bytes'] / 1024:.0f} KB |")
open(os.path.join(out, "derivatives.md"), "w").write("\n".join(lines) + "\n")
print(len(rows), "files")
