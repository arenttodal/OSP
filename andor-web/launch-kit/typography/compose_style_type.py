#!/usr/bin/env python3
"""Art Direction Gate R2: the ANDOR wordmark composited into a style frame (never baked).

  python3 compose_style_type.py --beauty SF.png --matte SF_matte.png --layout layout.json --out out.png

The wordmark is the brand's own: ANDOR set in Outfit ExtraBold, horizontally scaled 1.07, no
added tracking - exactly as the instrument draws its identity (shared/andor-ui InstrumentFrame).

A layout (JSON object) places it in the scene rather than on top of it:
  cap_height   cap height as a fraction of the frame height (oversized marks run off the frame)
  x, y         the cap box's left / top as fractions of the frame (may be negative: cropped)
  ink          RGB 0-255 of the letters at full light
  alpha        their opacity
  falloff      {"centre": [x, y], "radius": r, "floor": f}: the letters take the scene's light -
               full ink at the centre, fading toward `floor` beyond `radius` (fractions of the
               frame width), so the mark sits in the same pool of light as the instrument
  blur         defocus in px at 1920 wide (the mark is further away than the instrument)
  behind       true: the instrument occludes it through the matte (its soft edges included)
  mode         "over" (light ink on dark) or "multiply" (printed ink on a light wall)
The composite is  beauty * a + (beauty with the mark) * (1 - a)  through the instrument's alpha.
"""
import argparse
import json
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "Outfit-ExtraBold.ttf")
H_SCALE = 1.07


def wordmark(cap_px, text="ANDOR"):
    """The wordmark as an L mask whose cap height is cap_px (ExtraBold, 1.07 horizontal scale)."""
    size = 400
    f = ImageFont.truetype(FONT, size)
    bb = f.getbbox(text)
    im = Image.new("L", (bb[2] - bb[0] + 40, bb[3] - bb[1] + 40), 0)
    ImageDraw.Draw(im).text((20 - bb[0], 20 - bb[1]), text, font=f, fill=255)
    im = im.crop(im.getbbox())
    k = cap_px / im.height
    return im.resize((max(1, round(im.width * k * H_SCALE)), max(1, round(cap_px))), Image.LANCZOS)


def compose(beauty, matte, L):
    W, H = beauty.size
    s = W / 1920.0
    mark = wordmark(L["cap_height"] * H, L.get("text", "ANDOR"))
    layer = Image.new("L", (W, H), 0)
    layer.paste(mark, (round(L["x"] * W), round(L["y"] * H)))
    # occlusion is measured on the letter shapes, not on the faded ink, so it stays meaningful while a pass fades in
    shape = layer.point(lambda v: 255 if v > 127 else 0)
    if L.get("blur", 0) > 0:
        layer = layer.filter(ImageFilter.GaussianBlur(L["blur"] * s))
    a = L.get("alpha", 1.0)
    fo = L.get("falloff")
    if fo:
        cx, cy = fo["centre"][0] * W, fo["centre"][1] * H
        r, flo = fo["radius"] * W, fo.get("floor", 0.25)
        g = Image.new("L", (W // 8, H // 8))
        g.putdata([round(255 * (flo + (1 - flo) * math.exp(-(((x * 8 - cx) ** 2 + (y * 8 - cy) ** 2) / (r * r)))))
                   for y in range(H // 8) for x in range(W // 8)])
        layer = ImageChops.multiply(layer, g.resize((W, H), Image.BICUBIC))
    layer = layer.point(lambda v: round(v * a))
    if L.get("mode", "over") == "multiply":
        k = 1 - L.get("darken", 0.3)
        ink = layer.point(lambda v: round(255 - v * k))
        marked = ImageChops.multiply(beauty, Image.merge("RGB", (ink, ink, ink)))
    else:
        marked = Image.composite(Image.new("RGB", (W, H), tuple(L.get("ink", [200, 192, 180]))), beauty, layer)
    if L.get("behind", True) and matte is not None:
        alpha = matte.getchannel("A")
        out = Image.composite(beauty, marked, alpha)
        cover = ImageChops.multiply(shape, alpha.point(lambda v: 255 if v > 127 else 0))
        occl = sum(cover.histogram()[128:]) / max(1, sum(shape.histogram()[128:]))
    else:
        out, occl = marked, 0.0
    bbox = [round(L["x"] * W), round(L["y"] * H), round(L["x"] * W) + mark.width, round(L["y"] * H) + mark.height]
    return out, {"occlusion": round(occl, 3), "mark_box_px": bbox, "cropped": bbox[0] < 0 or bbox[2] > W or bbox[1] < 0 or bbox[3] > H}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--beauty", required=True)
    ap.add_argument("--matte")
    ap.add_argument("--layout", required=True, help="a JSON object, or a path to one")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    L = json.load(open(a.layout)) if os.path.exists(a.layout) else json.loads(a.layout)
    b = Image.open(a.beauty).convert("RGB")
    m = Image.open(a.matte) if a.matte else None
    if m is not None and m.size != b.size:
        m = m.resize(b.size, Image.LANCZOS)
    out, meta = compose(b, m, L)
    out.save(a.out)
    print(json.dumps(meta))
