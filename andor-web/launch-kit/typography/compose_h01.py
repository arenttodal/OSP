#!/usr/bin/env python3
"""H01 Front-Facing Monument: composites the ANDOR wordmark behind the instrument (through its
rendered matte) and, for the desktop variant, the website headline and call to action.

  python3 compose_h01.py --renders <dir> --mattes <dir> --out <dir>

Layouts: style/h01_layouts.json, keyed by spec id. The wordmark is centred on the frame (the
instrument faces the viewer, so the type is symmetric too); `y` is its cap box's top and
`cap_height` its height, both as fractions of the frame height. The occlusion it reports is the
share of the letters the instrument covers.
"""
import argparse
import json
import os

from PIL import Image, ImageDraw, ImageFont

from compose_style_type import H_SCALE, compose, wordmark

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "fonts")
LAYOUTS = json.load(open(os.path.join(HERE, "style", "h01_layouts.json")))


def centred(L, W, H):
    """the layout with x set so the wordmark is centred on the frame."""
    w = wordmark(L["cap_height"] * H).width
    return dict(L, x=(W - w) / 2.0 / W)


def web(img, spec):
    """the desktop mock: headline (EB Garamond) and call to action (Inter Tight) in the free top band."""
    W, H = img.size
    d = ImageDraw.Draw(img)
    ink = tuple(spec.get("ink", [40, 36, 32]))
    f = ImageFont.truetype(os.path.join(FONTS, "EBGaramond-Regular.ttf"), round(spec["headline_px"] * H))
    y = spec["headline_y"] * H
    for line in spec["headline"]:
        bb = d.textbbox((0, 0), line, font=f)
        d.text(((W - (bb[2] - bb[0])) / 2 - bb[0], y - bb[1]), line, font=f, fill=ink)
        y += spec["headline_px"] * H * 1.12
    fb = ImageFont.truetype(os.path.join(FONTS, "InterTight-760.ttf"), round(spec["cta_px"] * H))
    bb = d.textbbox((0, 0), spec["cta"], font=fb)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    pw, ph = th * 1.6, th * 1.05
    cy = y + spec["cta_gap"] * H
    box = [(W - tw) / 2 - pw, cy, (W + tw) / 2 + pw, cy + th + 2 * ph]
    d.rounded_rectangle(box, radius=(box[3] - box[1]) / 2, fill=ink)
    d.text(((W - tw) / 2 - bb[0], cy + ph - bb[1]), spec["cta"], font=fb, fill=tuple(spec.get("cta_ink", [246, 242, 236])))
    return {"headline_bottom_frac": round(y / H, 3), "cta_box_frac": [round(v / (W if i % 2 == 0 else H), 3) for i, v in enumerate(box)]}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--renders", required=True)
    ap.add_argument("--mattes", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    meta = {}
    for fid, L in LAYOUTS.items():
        if fid.startswith("_"):
            continue
        bp = os.path.join(a.renders, f"{fid}_still_t0.00.png")
        mp = os.path.join(a.mattes, f"{fid}_still_t0.00.png")
        if not (os.path.exists(bp) and os.path.exists(mp)):
            continue
        beauty = Image.open(bp).convert("RGB")
        matte = Image.open(mp)
        if matte.size != beauty.size:
            matte = matte.resize(beauty.size, Image.LANCZOS)
        W, H = beauty.size
        Lw = centred(L["wordmark"], W, H)
        img, m = compose(beauty, matte, Lw)
        img.save(os.path.join(a.out, f"{fid}_type.png"))
        rec = {"wordmark": dict(Lw, occlusion=m["occlusion"], mark_box_px=m["mark_box_px"]),
               "instrument_box_px": list(matte.getchannel("A").getbbox())}
        if "web" in L:
            w = img.copy()
            rec["web"] = web(w, L["web"])
            w.save(os.path.join(a.out, f"{fid}_web.png"))
        meta[fid] = rec
        print(fid, "occlusion", m["occlusion"])
    json.dump(meta, open(os.path.join(a.out, "h01_typography_meta.json"), "w"), indent=1)
