"""Interface audit of the campaign stills: each camera rendered with the 17 Nov interface and the frozen beta.
For each shot: the share of the frame that changes visibly (> 12 levels after a 3 px blur, so render noise does
not count), where it is, and a sheet: 17 Nov | beta | change (red)."""
import glob, json, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
D = sys.argv[1]; OUT = sys.argv[2]
rows, res = [], {}
for a in sorted(glob.glob(os.path.join(D, "*__current.png"))):
    shot = os.path.basename(a).split("__")[0]
    b = a.replace("__current", "__0.9.0-beta.1")
    if not os.path.exists(b): continue
    A, B = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    fa = np.asarray(A.filter(ImageFilter.GaussianBlur(1.5)), float); fb = np.asarray(B.filter(ImageFilter.GaussianBlur(1.5)), float)
    d = np.abs(fa - fb).max(axis=2)
    m = d > 12
    share = float(m.mean())
    ys, xs = np.nonzero(m)
    box = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())] if len(xs) else None
    # the changed area relative to the frame, and its largest connected blob size (pixels, at 25 %)
    res[shot] = {"changed_share_pct": round(100 * share, 2), "changed_box_px_at_25pct": box, "size": A.size,
                 "max_change_levels": round(float(d.max()), 1)}
    heat = Image.fromarray(np.uint8(np.clip(d * 4, 0, 255)))
    over = Image.composite(Image.new("RGB", A.size, (230, 40, 30)), B.point(lambda v: v // 3), heat)
    w = 420; h = round(A.height * w / A.width)
    tile = Image.new("RGB", (3 * w + 16, h + 24), (250, 248, 244)); dr = ImageDraw.Draw(tile)
    for i, im in enumerate((A, B, over)):
        tile.paste(im.resize((w, h), Image.LANCZOS), (i * (w + 8), 24))
    dr.text((4, 6), f"{shot}: 17 Nov | frozen beta | change ({res[shot]['changed_share_pct']} % of frame)", fill=(40, 36, 32))
    rows.append(tile)
W = max(r.width for r in rows); H = sum(r.height for r in rows)
sheet = Image.new("RGB", (W, H), (250, 248, 244)); y = 0
for r in rows: sheet.paste(r, (0, y)); y += r.height
sheet.save(OUT + ".jpg", quality=88)
json.dump(res, open(OUT + ".json", "w"), indent=1)
for k, v in res.items(): print(f"{k:14s} {v['changed_share_pct']:6.2f} %  max {v['max_change_levels']:5.1f}  box {v['changed_box_px_at_25pct']}")
