"""Packs the rendered ceramic pieces into the plugin's runtime resource.

    python3 pack.py            # Python 3 + numpy + Pillow (no Blender)

Reads tools/blender-assets/output/ceramic/raw/ (render.py) and writes
shared/andor-ui/Resources/skin/ceramic-skin.zip (stored, not deflated: PNG is compressed
already) with manifest.json and one image per piece and level:

- slabs and lenses keep their straight alpha (the part and its contact shadow);
- pockets become overlays: from the renders with and without the recess, the least
  opaque colour that turns the plain surface into the recessed one (black-ish where it
  shades, white-ish where the lip catches the light), so the recess can be laid over any
  similar surface;
- the window's background stays opaque (JPEG).

Pieces rendered at 4 px per design px are also stored at 2 (an exact 2 x 2 average), so
the plugin starts from the nearest level and resamples by area to the physical size.
"""

import io
import json
import os
import sys
import zipfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
import pieces as P  # noqa: E402

RAW = os.path.join(REPO, "tools", "blender-assets", "output", "ceramic", "raw")
OUT = os.path.join(REPO, "shared", "andor-ui", "Resources", "skin", "ceramic-skin.zip")
SCHEMA = 1


def load(path):
    return np.asarray(Image.open(path).convert("RGBA")).astype(np.float64) / 255.0


def overlay(with_, without):
    """Straight-alpha RGBA o with  o over without == with_ (per pixel, least alpha)."""
    w, b = with_[..., :3], without[..., :3]
    eps = 1e-6
    dark = np.max((b - w) / np.maximum(b, eps), axis=-1)
    light = np.max((w - b) / np.maximum(1.0 - b, eps), axis=-1)
    a = np.clip(np.maximum(np.maximum(dark, light), 0.0), 0.0, 1.0)
    a[a < 3.0 / 255.0] = 0.0   # the two renders' sampling noise on the flat surface
    safe = np.maximum(a, eps)[..., None]
    c = np.clip((w - (1.0 - a[..., None]) * b) / safe, 0.0, 1.0)
    c[a == 0.0] = 0.0
    return np.concatenate([c, a[..., None]], axis=-1)


def feather(rgba, margin_px):
    """Fades the alpha to nothing over the outer half of the margin: a shadow's faint tail,
    cut off by the image's edge, would otherwise draw the image's outline."""
    h, w = rgba.shape[:2]
    band = max(1.0, 0.5 * margin_px)
    y = np.arange(h)[:, None]
    x = np.arange(w)[None, :]
    d = np.minimum(np.minimum(x, w - 1 - x), np.minimum(y, h - 1 - y)).astype(np.float64)
    ramp = np.clip(d / band, 0.0, 1.0)
    ramp = ramp * ramp * (3.0 - 2.0 * ramp)
    out = rgba.copy()
    out[..., 3] *= ramp
    return out


def halve(rgba):
    """An exact 2 x 2 area average (in premultiplied space), straight alpha out."""
    h, w = rgba.shape[0] // 2 * 2, rgba.shape[1] // 2 * 2
    x = rgba[:h, :w]
    pre = np.concatenate([x[..., :3] * x[..., 3:], x[..., 3:]], axis=-1)
    pre = pre.reshape(h // 2, 2, w // 2, 2, 4).mean(axis=(1, 3))
    a = pre[..., 3:]
    rgb = np.where(a > 1e-6, pre[..., :3] / np.maximum(a, 1e-6), 0.0)
    return np.concatenate([rgb, a], axis=-1)


def encode(rgba, opaque=False):
    """PNG with alpha; the opaque background as JPEG (a fine, noisy surface: a fraction of
    the PNG's size, no visible loss at quality 94 without chroma subsampling)."""
    data = np.clip(np.round(rgba * 255.0), 0, 255).astype(np.uint8)
    buf = io.BytesIO()
    if opaque:
        Image.fromarray(data[..., :3], "RGB").save(buf, "JPEG", quality=94, subsampling=0, optimize=True)
    else:
        Image.fromarray(data, "RGBA").save(buf, "PNG", optimize=True)
    return buf.getvalue()


def face_colour(rgba, margin_px):
    """The part's face in the middle (for the manifest: what it shows, as #rrggbb)."""
    h, w = rgba.shape[:2]
    y0, y1 = int(margin_px + 0.35 * (h - 2 * margin_px)), int(margin_px + 0.65 * (h - 2 * margin_px)) + 1
    x0, x1 = int(margin_px + 0.35 * (w - 2 * margin_px)), int(margin_px + 0.65 * (w - 2 * margin_px)) + 1
    c = rgba[y0:y1, x0:x1, :3].reshape(-1, 3).mean(axis=0)
    return "#" + "".join(f"{int(round(v * 255)):02x}" for v in c)


def main():
    manifest = {"schemaVersion": SCHEMA, "name": "ceramic", "direction": P.DIRECTION, "pieces": []}
    files = {}
    missing = []
    for p in P.pieces():
        name = p["name"]
        if p["kind"] == "pocket":
            paths = [os.path.join(RAW, f"{name}.{s}.png") for s in ("with", "without")]
            if not all(os.path.exists(x) for x in paths):
                missing.append(name)
                continue
            img = feather(overlay(load(paths[0]), load(paths[1])), p["margin"] * p["scale"])
            kind = "overlay"
        else:
            path = os.path.join(RAW, name + ".png")
            if not os.path.exists(path):
                missing.append(name)
                continue
            img = load(path)
            kind = "opaque" if p["kind"] == "opaque" else "alpha"
            if kind == "alpha":
                img = feather(img, p["margin"] * p["scale"])
        entry = {"name": name, "kind": kind, "rect": [float(v) for v in p["rect"]], "margin": float(p["margin"]),
                 "insets": [float(v) for v in p["insets"]], "levels": []}
        if "radius" in p:
            entry["radius"] = float(p["radius"])
        if kind != "overlay":
            entry["face"] = face_colour(img, p["margin"] * p["scale"])
        if kind == "opaque":
            entry["edge"] = "#" + "".join(f"{int(round(v * 255)):02x}" for v in img[2, 2, :3])
        scales = [p["scale"]] + ([2] if p["scale"] == 4 else [])
        level = img
        for s in scales:
            if s != scales[0]:
                level = halve(level)
            file = f"pieces/{name}@{s}x." + ("jpg" if kind == "opaque" else "png")
            files[file] = encode(level, opaque=kind == "opaque")
            entry["levels"].append({"scale": s, "file": file, "width": int(level.shape[1]), "height": int(level.shape[0])})
        manifest["pieces"].append(entry)
    if missing:
        raise SystemExit("not rendered yet: " + ", ".join(missing))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_STORED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=1))
        for file, data in sorted(files.items()):
            z.writestr(file, data)
    total = sum(len(d) for d in files.values())
    print(f"{len(manifest['pieces'])} pieces, {len(files)} images, {total / 1e6:.2f} MB -> {os.path.relpath(OUT, REPO)}")


if __name__ == "__main__":
    main()
