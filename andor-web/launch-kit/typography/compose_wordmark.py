#!/usr/bin/env python3
"""Cinematographic typography for R01 Grand Reveal: the ANDOR wordmark behind the instrument.

  python3 compose_wordmark.py --beauty DIR --matte DIR --out DIR --look ivory|dark
                              [--layout centre|desktop] [--frames A B] [--sheet]

Inputs are the rig's passes for the same shot (cine_rig.py): `beauty/NNNN.png` (the clean master,
no typography) and `matte/NNNN.png` (RGBA, the instrument's alpha with the environment held out).

  composite = beauty * a + (beauty with the wordmark) * (1 - a)

so the instrument occludes the type exactly, its soft edges included, and the shadow and
reflections stay the master's own. The wordmark lives on the far wall: it does not move with
the product. It is sized and placed once, from the closing frame's matte, so that it ends fully
readable above the instrument; during the move the camera's withdrawal is echoed by a slow
scale (1.06 -> 1.00) about the wordmark's centre, and the instrument's descent uncovers it.

Fonts (OFL, ./fonts): Inter Tight 760 for ANDOR (the campaign wordmark), EB Garamond for the
headline. Outfit stays the plugin's own identity and is never used here.
"""
import argparse
import json
import os

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_MARK = os.path.join(HERE, "fonts", "InterTight-760.ttf")
FONT_LIGHT = os.path.join(HERE, "fonts", "InterTight-300.ttf")
FONT_LINE = os.path.join(HERE, "fonts", "EBGaramond-Regular.ttf")
HEADLINE = ["One sound.", "An entire instrument."]
# Ink: a printed, matte presence on the wall, never a glow.
LOOKS = {
    "ivory": {"mark": (60, 54, 48), "mark_alpha": 0.86, "line": (44, 40, 36)},
    "dark": {"mark": (176, 166, 152), "mark_alpha": 0.62, "line": (206, 198, 186)},
}

ap = argparse.ArgumentParser()
ap.add_argument("--beauty", required=True)
ap.add_argument("--matte", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--look", default="auto", choices=list(LOOKS) + ["auto"], help="auto: from the wall behind the wordmark at the close")
ap.add_argument("--layout", default="centre", choices=["centre", "desktop"])
ap.add_argument("--frames", type=int, nargs=2)
ap.add_argument("--width-frac", type=float, default=0.78, help="wordmark width as a fraction of the frame")
ap.add_argument("--gap-frac", type=float, default=0.025, help="closing gap between baseline and instrument")
ap.add_argument("--scale-from", type=float, default=1.06)
a = ap.parse_args()

names = sorted(f for f in os.listdir(a.beauty) if f.endswith(".png") and os.path.exists(os.path.join(a.matte, f)))
if a.frames:
    names = [n for n in names if a.frames[0] <= int(n[:4]) <= a.frames[1]]
if not names:
    raise SystemExit("no frames with both beauty and matte")
os.makedirs(a.out, exist_ok=True)
W, H = Image.open(os.path.join(a.beauty, names[0])).size


def top_of_instrument(alpha, x0, x1):
    """The highest row (px) where the instrument covers the columns x0..x1."""
    bb = alpha.crop((x0, 0, x1, H)).point(lambda v: 255 if v > 24 else 0).getbbox()
    return bb[1] if bb else H


# ---- placement from the closing frame ---------------------------------------------------------
close_alpha = Image.open(os.path.join(a.matte, names[-1])).getchannel("A")
x_mark0 = round(W * (0.06 if a.layout == "desktop" else (1 - a.width_frac) / 2))
mark_w = round(W * (0.62 if a.layout == "desktop" else a.width_frac))
top_inst = top_of_instrument(close_alpha, x_mark0, x_mark0 + mark_w)
size = 200
font = ImageFont.truetype(FONT_MARK, size)
bb = font.getbbox("ANDOR")
size = int(size * mark_w / (bb[2] - bb[0]))
font = ImageFont.truetype(FONT_MARK, size)
bb = font.getbbox("ANDOR")
cap_h = bb[3] - bb[1]
baseline = top_inst - round(H * a.gap_frac)
# keep a top margin: shrink until the cap height fits
while baseline - cap_h < H * 0.07 and size > 20:
    size = int(size * 0.96)
    font = ImageFont.truetype(FONT_MARK, size)
    bb = font.getbbox("ANDOR")
    cap_h = bb[3] - bb[1]
mark_w_real = bb[2] - bb[0]
mark_x = x_mark0 + (mark_w - mark_w_real) // 2 if a.layout == "centre" else x_mark0
mark_y = baseline - cap_h
# letter spacing: the wordmark is set tight (Inter Tight's own spacing), no tracking added.
layer = Image.new("L", (W, H), 0)
ImageDraw.Draw(layer).text((mark_x - bb[0], mark_y - bb[1]), "ANDOR", font=font, fill=255)
centre = (mark_x + mark_w_real / 2, mark_y + cap_h / 2)

line_layer = None
if a.layout == "desktop":
    # Headline in the quiet space below the wordmark, left column; it never meets the instrument.
    ls = round(W * 0.034)
    lf = ImageFont.truetype(FONT_LINE, ls)
    line_layer = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(line_layer)
    y = round(H * 0.60)
    for t in HEADLINE:
        d.text((x_mark0, y), t, font=lf, fill=255)
        y += round(ls * 1.18)

meta = {"layout": a.layout, "look": a.look, "frame": [W, H], "wordmark": {"font": "Inter Tight 760", "size_px": size,
        "box_px": [mark_x, mark_y, mark_x + mark_w_real, mark_y + cap_h], "baseline_px": baseline,
        "closing_top_of_instrument_px": top_inst, "scale_from": a.scale_from},
        "headline": HEADLINE if line_layer else None, "frames": [names[0], names[-1]], "occlusion": []}

if a.look == "auto":
    # Dark ink on a light wall, light ink on a dark one: the wall's mean luminance under the mark.
    wall = Image.open(os.path.join(a.beauty, names[-1])).convert("L").crop((mark_x, mark_y, mark_x + mark_w_real, baseline))
    a.look = "ivory" if sum(i * c for i, c in enumerate(wall.histogram())) / max(1, wall.width * wall.height) > 110 else "dark"
    meta["look"] = a.look
L = LOOKS[a.look]
mark_rgb = Image.new("RGB", (W, H), L["mark"])
line_rgb = Image.new("RGB", (W, H), L["line"])
for i, n in enumerate(names):
    u = i / max(1, len(names) - 1)
    u = u * u * (3 - 2 * u)
    s = a.scale_from + (1.0 - a.scale_from) * u
    # scale the wall layer about the wordmark centre (an affine on the type only)
    lay = layer.transform((W, H), Image.AFFINE, (1 / s, 0, centre[0] - centre[0] / s, 0, 1 / s, centre[1] - centre[1] / s),
                          resample=Image.BICUBIC)
    beauty = Image.open(os.path.join(a.beauty, n)).convert("RGB")
    alpha = Image.open(os.path.join(a.matte, n)).getchannel("A")
    behind = Image.composite(mark_rgb, beauty, lay.point(lambda v: int(v * L["mark_alpha"])))
    if line_layer is not None:
        behind = Image.composite(line_rgb, behind, line_layer)
    Image.composite(beauty, behind, alpha).save(os.path.join(a.out, n))
    # occlusion: the fraction of the wordmark's ink the instrument covers
    ink = sum(lay.histogram()[128:])
    covered = sum(ImageChops.multiply(lay.point(lambda v: 255 if v > 127 else 0), alpha.point(lambda v: 255 if v > 127 else 0)).histogram()[128:])
    meta["occlusion"].append(round(covered / max(1, ink), 4))
json.dump(meta, open(os.path.join(a.out, "typography_meta.json"), "w"), indent=1)
print("composited", len(names), "frames; occlusion open/close", meta["occlusion"][0], meta["occlusion"][-1])
