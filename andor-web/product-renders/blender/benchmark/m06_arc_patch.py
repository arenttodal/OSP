"""M06: keep the approved R2 master; replace only the macro arc at the top edge with the same pixels from the
frozen-interface render. Today's scene does not reproduce R2's sensor sheen exactly (about 2 levels overall), so a
full re-render would change an approved look; the arc lies on smooth, out-of-focus panel where a level-matched,
feathered patch is seamless. Writes <out>/C3_M06_<v>.png (8-bit) and a check sheet; prints how much changed."""
import glob, json, os, sys
import numpy as np
from PIL import Image, ImageFilter, ImageDraw
S, OUT = sys.argv[1], sys.argv[2]
K = glob.glob(os.path.join(S, "old.LGY8/kit/*/07_website-asset-kit_v1/01_framer_upload/"))[0]
os.makedirs(OUT, exist_ok=True)
rep = {}
for v, kit in (("sharp", "sharp"), ("cine", "cinematic")):
    o = Image.open(K + f"andor-osp_detail-touch-strips_{kit}.webp").convert("RGB")
    n = Image.open(os.path.join(S, "c3fix", f"C3_M06_{v}.png")).convert("RGB")
    A, B = np.asarray(o, float), np.asarray(n, float)
    H, W = A.shape[:2]
    # the arc: where the hue differs in the top band (orange in R2, sage now); everything else is excluded
    ba, bb = np.asarray(o.filter(ImageFilter.GaussianBlur(4)), float), np.asarray(n.filter(ImageFilter.GaussianBlur(4)), float)
    chroma = lambda X: X[..., 0] - X[..., 1]                 # red minus green: high for orange, negative for sage
    d = np.abs(chroma(ba) - chroma(bb))
    region = np.zeros((H, W), bool); region[: int(0.2 * H), : int(0.45 * W)] = True
    m = (d > 6) & region
    mask = Image.fromarray(np.uint8(m * 255)).filter(ImageFilter.MaxFilter(31)).filter(ImageFilter.GaussianBlur(14))
    M = np.asarray(mask, float)[..., None] / 255.0
    # level match: today's render is ~2 levels darker; correct it on a ring just outside the patch
    ring = (np.asarray(mask.filter(ImageFilter.MaxFilter(41)), float) > 0) & (M[..., 0] == 0)
    off = (A[ring] - B[ring]).mean(0) if ring.any() else np.zeros(3)
    P = np.clip(A * (1 - M) + (B + off) * M, 0, 255)
    out = Image.fromarray(np.uint8(np.round(P)))
    out.save(os.path.join(OUT, f"C3_M06_{v}.png"))
    diff = np.abs(np.asarray(out, float) - A).max(2)
    ys, xs = np.nonzero(diff > 1)
    rep[v] = {"level_offset": off.round(2).tolist(), "patched_share_pct": round(100 * float((M[..., 0] > 0.01).mean()), 3),
              "changed_box_px": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())] if len(xs) else None,
              "unchanged_outside_patch": bool((diff[M[..., 0] == 0] <= 0.5).all())}
    # check sheet: original | patched, top-left crop, 1:1
    c = (0, 0, int(0.45 * W), int(0.2 * H))
    sh = Image.new("RGB", (2 * (c[2] - c[0]) + 10, c[3] - c[1] + 20), (250, 248, 244))
    sh.paste(o.crop(c), (0, 20)); sh.paste(out.crop(c), (c[2] - c[0] + 10, 20))
    ImageDraw.Draw(sh).text((4, 4), f"M06 {v}: approved R2 | arc from the frozen interface (rest unchanged)", fill=(40, 36, 32))
    sh.save(os.path.join(OUT, f"check_M06_{v}.jpg"), quality=90)
json.dump(rep, open(os.path.join(OUT, "patch_report.json"), "w"), indent=1)
print(json.dumps(rep, indent=1))
