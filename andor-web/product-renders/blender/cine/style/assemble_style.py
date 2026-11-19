#!/usr/bin/env python3
"""Packages Art Direction Gate R2: the 12 style frames, the Monument typography alternatives and the
numbered contact sheets (system Python + Pillow).

  python3 assemble_style.py --renders <dir with SF_*_still_t0.00.png> --mattes <dir> --out <package> [--version 1]

Per frame: the clean image (16-bit PNG master + JPG). Monument (A): the instrument's matte and the
wordmark alternatives of launch-kit/typography/style/monument_layouts.json, composited through it.
Names follow Launch Kit v2 (a draft never carries a final-implying name).
"""
import argparse
import json
import os
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
TYPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "launch-kit", "typography"))
sys.path.insert(0, TYPO)
from compose_style_type import compose  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--renders", required=True)
ap.add_argument("--mattes", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--version", type=int, default=1)
a = ap.parse_args()
V = f"v{a.version:02d}"
LAYOUTS = json.load(open(os.path.join(TYPO, "style", "monument_layouts.json")))
LABEL_FONT = os.path.join(TYPO, "fonts", "InterTight-300.ttf")
ASPECT = {"SF_B3_vertical": "9x16_1080w"}


def slug(t):
    return "".join(ch if ch.isalnum() else "-" for ch in t.lower()).strip("-").replace("--", "-")


def name(fid, variant, kind, ext):
    return f"andor-osp_{fid.replace('_', '-')}_{slug(variant)}{'-' + kind if kind else ''}_{ASPECT.get(fid, '16x9_1920w')}_draft_{V}.{ext}"


def sheet(items, out, cols, title):
    """items: (number label, caption, PIL image). A numbered contact sheet on warm paper."""
    tw, th, pad, cap = 600, 338, 18, 40
    rows = (len(items) + cols - 1) // cols
    S = Image.new("RGB", (cols * (tw + pad) + pad, rows * (th + cap + pad) + pad + 56), (246, 243, 238))
    d = ImageDraw.Draw(S)
    fb, fs = ImageFont.truetype(LABEL_FONT, 26), ImageFont.truetype(LABEL_FONT, 17)
    d.text((pad, 16), title, font=fb, fill=(40, 36, 32))
    for k, (num, caption, im) in enumerate(items):
        x, y = pad + (k % cols) * (tw + pad), 56 + pad + (k // cols) * (th + cap + pad)
        t = im.convert("RGB")
        t.thumbnail((tw, th), Image.LANCZOS)   # a vertical frame keeps its proportions, centred in the tile
        S.paste(t, (x + (tw - t.width) // 2, y + (th - t.height) // 2))
        d.text((x, y + th + 8), num, font=fb, fill=(40, 36, 32))
        d.text((x + 44, y + th + 13), caption, font=fs, fill=(90, 84, 78))
    S.save(out, quality=92)


os.makedirs(a.out, exist_ok=True)
specs = {f[:-5]: json.load(open(os.path.join(HERE, "specs", f))) for f in sorted(os.listdir(os.path.join(HERE, "specs"))) if f.endswith(".json")}
index, items, typo_items = [], [], []
for n, (fid, s) in enumerate(specs.items(), 1):
    src = os.path.join(a.renders, f"{fid}_still_t0.00.png")
    if not os.path.exists(src):
        print("missing", fid)
        continue
    fam = s["family"].split()[0]
    d = os.path.join(a.out, f"{fam}-{slug(s['family'].split(' ', 1)[1])}")
    os.makedirs(d, exist_ok=True)
    im = Image.open(src)
    png = os.path.join(d, name(fid, s["variant"], "", "png"))
    shutil.copyfile(src, png)
    jpg = os.path.join(d, name(fid, s["variant"], "", "jpg"))
    rgb = im.convert("RGB") if im.mode != "I;16" else im.point(lambda v: v / 257).convert("RGB")
    rgb.save(jpg, quality=95)
    rec = {"number": n, "id": fid, "family": s["family"], "title": s["variant"], "rationale": s["rationale"],
           "clean_png": os.path.relpath(png, a.out), "clean_jpg": os.path.relpath(jpg, a.out)}
    items.append((f"{n:02d}", f"{fid.replace('SF_', '')} · {s['family'].split(' ', 1)[1]} · {s['variant']}", rgb))
    mp = os.path.join(a.mattes, f"{fid}_still_t0.00.png")
    if fid in LAYOUTS and os.path.exists(mp):
        matte = Image.open(mp)
        mout = os.path.join(d, name(fid, s["variant"], "matte", "png"))
        matte.getchannel("A").save(mout)
        rec["matte"] = os.path.relpath(mout, a.out)
        rec["typography"] = {}
        for k, L in LAYOUTS[fid].items():
            comp, meta = compose(rgb, matte, L)
            tp = os.path.join(d, name(fid, s["variant"], "type-" + k.lower(), "jpg"))
            comp.save(tp, quality=95)
            comp.save(tp[:-4] + ".png")
            rec["typography"][k] = dict(meta, file=os.path.relpath(tp, a.out), layout=L)
            typo_items.append((f"{n:02d}", f"{fid.replace('SF_', '')} · {k} · occlusion {meta['occlusion']:.0%}", comp))
    index.append(rec)
    print("packaged", fid)
sheet(items, os.path.join(a.out, f"andor-osp_style-frames-contact-sheet_numbered_draft_{V}.jpg"), 3,
      f"ANDOR · Art Direction Gate R2 · {len(items)} style frames (clean, no typography; B1 and D2 relit, B3 in three formats)")
if typo_items:
    sheet(typo_items, os.path.join(a.out, f"andor-osp_monument-typography-sheet_draft_{V}.jpg"), 2,
          "ANDOR · Monument · the wordmark behind the instrument (Outfit ExtraBold, composited through the matte)")
json.dump(index, open(os.path.join(a.out, "style_index.json"), "w"), indent=1)
print(len(index), "frames ->", a.out)
