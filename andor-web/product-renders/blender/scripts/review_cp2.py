"""Checkpoint 2 review exports (Pillow): the geometry-review renders as JPEGs, a before/after sheet
(pass 3.5 teak fascia vs the checkpoint 2 ivory front return, identical cameras, lights and
materials), and 100 % crops of the two front corners.

  python3 product-renders/blender/scripts/review_cp2.py <render dir> <old camera-B render> <out dir>

<render dir> holds new_*.png and old_*.png from andor_scene.py (old = --textures of the pass 3.5
capture, which rebuilds the fascia); <old camera-B render> is pass 3.5's G_neutral.png (same settings).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

src, old_b, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)

try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()

RENDERS = [
    ("01_camera_A_neutral", "new_A", "Camera A · full neutral studio render"),
    ("02_camera_B_neutral", "new_B", "Camera B · full neutral studio render"),
    ("03_front_edge_straight_on", "new_front_edge", "Straight on · the new bottom edge"),
    ("04_key_fronts_and_return", "new_keys34", "Three-quarter close-up · key fronts and the ivory return"),
    ("05_lower_left_corner", "new_corner_fl", "Lower-left corner · PITCH / MOD and the timber cheek"),
    ("06_lower_right_corner", "new_corner", "Lower-right corner · ARP and the timber cheek"),
]
for name, file, _ in RENDERS:
    Image.open(os.path.join(src, file + ".png")).convert("RGB").save(os.path.join(out, name + ".jpg"), quality=93, subsampling=0)

# 100 % crops of the cheek-to-return junctions (pixels of the 2000 x 1500 corner renders)
CROPS = {"05b_lower_left_junction_100pct": ("new_corner_fl", (380, 330, 1180, 780)),
         "06b_lower_right_junction_100pct": ("new_corner", (560, 330, 1360, 780))}
for name, (file, box) in CROPS.items():
    Image.open(os.path.join(src, file + ".png")).convert("RGB").crop(box).save(os.path.join(out, name + ".png"), optimize=True)

PAIRS = [
    ("Camera B", old_b, os.path.join(src, "new_B.png")),
    ("Straight on", os.path.join(src, "old_front_edge.png"), os.path.join(src, "new_front_edge.png")),
    ("Key fronts", os.path.join(src, "old_keys34.png"), os.path.join(src, "new_keys34.png")),
    ("Lower-left corner", os.path.join(src, "old_corner_fl.png"), os.path.join(src, "new_corner_fl.png")),
    ("Lower-right corner", os.path.join(src, "old_corner.png"), os.path.join(src, "new_corner.png")),
]
CELL_W = 1200
GAP = 24
rows = []
for label, a, b in PAIRS:
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    h = round(CELL_W * ia.height / ia.width)
    rows.append((label, ia.resize((CELL_W, h), Image.LANCZOS), ib.resize((CELL_W, h), Image.LANCZOS)))
H = GAP + 56 + sum(r[1].height + 46 + GAP for r in rows)
sheet = Image.new("RGB", (GAP * 3 + CELL_W * 2, H), (24, 22, 20))
d = ImageDraw.Draw(sheet)
d.text((GAP, GAP), "Before: pass 3.5 (footer + teak front fascia)", fill=(232, 228, 220), font=font)
d.text((GAP * 2 + CELL_W, GAP), "After: checkpoint 2 (approved GUI, ivory front return)", fill=(232, 228, 220), font=font)
y = GAP + 56
for label, ia, ib in rows:
    d.text((GAP, y), label, fill=(190, 184, 174), font=small)
    d.text((GAP * 2 + CELL_W, y), label + " · same camera, lights and materials", fill=(190, 184, 174), font=small)
    y += 46
    sheet.paste(ia, (GAP, y))
    sheet.paste(ib, (GAP * 2 + CELL_W, y))
    y += ia.height + GAP
sheet.save(os.path.join(out, "07_before_after_fascia_vs_ivory_return.jpg"), quality=90)

# overview of the six geometry views
thumbs = [(label, Image.open(os.path.join(out, name + ".jpg"))) for name, _, label in RENDERS]
TW, TH = 900, 675
ov = Image.new("RGB", (GAP + 3 * (TW + GAP), GAP + 2 * (TH + 50 + GAP)), (24, 22, 20))
d = ImageDraw.Draw(ov)
for i, (label, im) in enumerate(thumbs):
    x, y = GAP + (i % 3) * (TW + GAP), GAP + (i // 3) * (TH + 50 + GAP)
    t = im.copy()
    t.thumbnail((TW, TH), Image.LANCZOS)
    ov.paste(t, (x + (TW - t.width) // 2, y + 44 + (TH - t.height) // 2))
    d.text((x, y + 8), label, fill=(232, 228, 220), font=small)
ov.save(os.path.join(out, "00_checkpoint2_overview.jpg"), quality=88)
print("ok", out)
