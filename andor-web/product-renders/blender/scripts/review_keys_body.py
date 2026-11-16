"""Keyboard + front body round: review exports (Pillow). The five geometry views as JPEGs (A-E), a
previous/revised comparison sheet (F; identical cameras, lights, samples and resolution), 100 %
crops of the key fronts and both front corners, and a one-page overview.

  python3 product-renders/blender/scripts/review_keys_body.py <revised dir> <previous dir> <out dir>

Both dirs hold front_edge, keys34, corner_fl, corner and B renders: <previous dir> from
andor_scene_cp2.py (checkpoint 2, files named new_*.png), <revised dir> from andor_scene.py.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

new, prev, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()

VIEWS = [
    ("A_front_low_full_width", "front_edge", "A · Front, low angle, full width"),
    ("B_keyboard_closeup", "keys34", "B · Keyboard close-up: key fronts, black keys, front return"),
    ("C_left_front_corner", "corner_fl", "C · Left-front corner: cheek, shell, PITCH/MOD, return"),
    ("D_right_front_corner", "corner", "D · Right-front corner: cheek, return, buttons"),
    ("E_three_quarter_neutral", "B", "E · Neutral three-quarter, full product"),
]
for name, file, _ in VIEWS:
    Image.open(os.path.join(new, file + ".png")).convert("RGB").save(os.path.join(out, name + ".jpg"), quality=93, subsampling=0)

# 100 % crops (pixels of the 2000 x 1500 close-ups)
CROPS = {"B2_key_fronts_100pct": ("keys34", (300, 300, 1300, 950)),
         "C2_left_corner_100pct": ("corner_fl", (300, 150, 1300, 800)),
         "D2_right_corner_100pct": ("corner", (700, 250, 1700, 900))}
for name, (file, box) in CROPS.items():
    Image.open(os.path.join(new, file + ".png")).convert("RGB").crop(box).save(os.path.join(out, name + ".png"), optimize=True)

CELL_W, GAP = 1200, 24
rows = []
for _, file, label in VIEWS:
    a = Image.open(os.path.join(prev, "new_" + file + ".png")).convert("RGB")
    b = Image.open(os.path.join(new, file + ".png")).convert("RGB")
    h = round(CELL_W * a.height / a.width)
    rows.append((label, a.resize((CELL_W, h), Image.LANCZOS), b.resize((CELL_W, h), Image.LANCZOS)))
H = GAP + 56 + sum(r[1].height + 46 + GAP for r in rows)
sheet = Image.new("RGB", (GAP * 3 + CELL_W * 2, H), (24, 22, 20))
d = ImageDraw.Draw(sheet)
d.text((GAP, GAP), "Previous: checkpoint 2 (thin lip, flat keys, dark under-band)", fill=(232, 228, 220), font=font)
d.text((GAP * 2 + CELL_W, GAP), "Revised: keyboard + front body round", fill=(232, 228, 220), font=font)
y = GAP + 56
for label, a, b in rows:
    d.text((GAP, y), label, fill=(190, 184, 174), font=small)
    d.text((GAP * 2 + CELL_W, y), "same camera, lights and samples", fill=(190, 184, 174), font=small)
    y += 46
    sheet.paste(a, (GAP, y))
    sheet.paste(b, (GAP * 2 + CELL_W, y))
    y += a.height + GAP
sheet.save(os.path.join(out, "F_previous_vs_revised.jpg"), quality=90)

TW, TH = 900, 675
ov = Image.new("RGB", (GAP + 3 * (TW + GAP), GAP + 2 * (TH + 50 + GAP)), (24, 22, 20))
d = ImageDraw.Draw(ov)
for i, (name, _, label) in enumerate(VIEWS):
    x, y = GAP + (i % 3) * (TW + GAP), GAP + (i // 3) * (TH + 50 + GAP)
    t = Image.open(os.path.join(out, name + ".jpg"))
    t.thumbnail((TW, TH), Image.LANCZOS)
    ov.paste(t, (x + (TW - t.width) // 2, y + 44 + (TH - t.height) // 2))
    d.text((x, y + 8), label.split(":")[0], fill=(232, 228, 220), font=small)
ov.save(os.path.join(out, "00_overview.jpg"), quality=88)
print("ok", out)
