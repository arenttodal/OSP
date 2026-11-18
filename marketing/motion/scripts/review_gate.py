"""ANDOR Motion Library: the review set for a gate, from the shots' manifests.

  <python with Pillow> marketing/motion/scripts/review_gate.py --gate D1 --shots D01 D02 D03 [--media marketing/motion/media]

Writes marketing/motion/review/<gate>/ (manageable review assets, committed):
  contact_sheet.jpg      per shot: opening, middle and closing frames and the high-quality still;
                         then each reframe test's stills
  <id>_<open|middle|close>.jpg, <id>_hq.jpg, <reframe>_<t>.jpg   review stills (JPEG, long edge <= 1600 px)
  <id>_preview.mp4       the draft, re-encoded for review (H.264, CRF 23, faststart)
"""
import argparse
import json
import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ap = argparse.ArgumentParser()
ap.add_argument("--gate", required=True)
ap.add_argument("--shots", nargs="+", required=True)
ap.add_argument("--media", default=os.path.join(ROOT, "marketing", "motion", "media"))
args = ap.parse_args()
OUT = os.path.join(ROOT, "marketing", "motion", "review", args.gate)
os.makedirs(OUT, exist_ok=True)

BG, INK, SOFT = (238, 233, 224), (47, 44, 40), (128, 122, 113)


def font(size, bold=False):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def review_jpg(src, dst, edge=1600):
    im = Image.open(src).convert("RGB")
    im.thumbnail((edge, edge), Image.LANCZOS)
    im.save(dst, quality=90)
    return im


rows = []
for sid in args.shots:
    man = json.load(open(os.path.join(ROOT, "marketing", "motion", "manifests", f"{sid}.json")))
    stills = os.path.join(ROOT, man["files"]["stills"])
    cells = []
    for tag in ("open", "middle", "close"):
        p = os.path.join(stills, f"{sid}_{tag}.png")
        if os.path.exists(p):
            frame = round({"open": 0.0, "middle": 0.5, "close": 1.0}[tag] * man["frameRange"][1])
            cells.append((review_jpg(p, os.path.join(OUT, f"{sid}_{tag}.jpg")), f"{tag} · frame {frame}"))
    hq = [f for f in os.listdir(stills) if f.startswith(f"{sid}_hq_")] if os.path.isdir(stills) else []
    if hq:
        cells.append((review_jpg(os.path.join(stills, hq[0]), os.path.join(OUT, f"{sid}_hq.jpg")), "high-quality still · " + hq[0].split("_hq_")[1][:-4]))
    rows.append((f"{sid}  {man['title']}  ({man['version']}, {man['seconds']:g} s at {man['fps']} fps)", cells))
    for rf in man.get("reframes", []):
        rcells = []
        for p in rf["stills"]:
            name = os.path.splitext(os.path.basename(p))[0]
            rcells.append((review_jpg(os.path.join(ROOT, p), os.path.join(OUT, name + ".jpg")), name.split("_")[-1].lstrip("0") + " %" if not name.endswith("_000") else "0 %"))
        rows.append((f"{rf['id']}  {rf['purpose']}", rcells))
    mp4 = os.path.join(ROOT, man["files"]["mp4"])
    if os.path.exists(mp4):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-c:v", "libx264", "-preset", "slow", "-crf", "23", "-pix_fmt", "yuv420p",
                        "-movflags", "+faststart", os.path.join(OUT, f"{sid}_preview.mp4")], check=True)

# The sheet: each row's cells at one height, labelled.
H, PAD, LABEL = 300, 24, 46
width = 0
layout = []
for title, cells in rows:
    scaled = [(im.resize((max(1, round(im.width * H / im.height)), H), Image.LANCZOS), cap) for im, cap in cells]
    layout.append((title, scaled))
    width = max(width, PAD + sum(im.width + PAD for im, _ in scaled))
height = PAD + sum(LABEL + H + 30 + PAD for _ in layout) + 40
sheet = Image.new("RGB", (max(width, 1200), height), BG)
d = ImageDraw.Draw(sheet)
d.text((PAD, 12), f"ANDOR Motion Library · Gate {args.gate} draft review (not for publication)", font=font(20, True), fill=INK)
y = 40 + PAD
for title, scaled in layout:
    d.text((PAD, y), title, font=font(17, True), fill=INK)
    x, y = PAD, y + LABEL - 12
    for im, cap in scaled:
        sheet.paste(im, (x, y))
        d.text((x, y + H + 6), cap, font=font(14), fill=SOFT)
        x += im.width + PAD
    y += H + 30 + PAD
sheet.save(os.path.join(OUT, "contact_sheet.jpg"), quality=90)
print("review set:", OUT)
