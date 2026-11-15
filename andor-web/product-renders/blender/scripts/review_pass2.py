"""Pass 2 review exports (Pillow): optimised copies of the review renders, 100 % detail crops from the
neutral studio render, and one labelled contact sheet.

  python3 product-renders/blender/scripts/review_pass2.py <render dir> <repo renders dir> <crops.json>

crops.json: {"name": [x0, y0, x1, y1], ...} in pixels of 02_neutral_B.png (chosen by eye per pass).
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

src, dst, crops_file = sys.argv[1:4]
review = os.path.join(dst, "review_pass2")
os.makedirs(review, exist_ok=True)
crops = json.load(open(crops_file))

LABELS = {
    "01_clay_B": "01 Clay · camera B",
    "02_neutral_B": "02 Neutral studio · camera B",
    "06_neutral_A": "Camera A (pass 1's angle) · neutral",
    "03_macros": "03 The six macro knobs",
    "04_display": "04 Layer B display",
    "05_corner": "05 Right rail and front corner",
    "08_dark_hero_A": "Dark 9:16 · camera A (pass 1 framing)",
    "07_dark_hero_B": "07 Dark 9:16 · camera B",
}
for name in LABELS:
    im = Image.open(os.path.join(src, name + ".png")).convert("RGB")
    im.save(os.path.join(review, name + ".jpg"), quality=92, subsampling=0)

alpha = Image.open(os.path.join(src, "09_alpha_hero_B.png")).convert("RGBA")
alpha.save(os.path.join(review, "09_alpha_hero_B.png"), optimize=True)

neutral = Image.open(os.path.join(src, "02_neutral_B.png")).convert("RGB")
tiles = []
for name, box in crops.items():
    c = neutral.crop(tuple(box))
    c.save(os.path.join(review, f"crop_{name}.png"), optimize=True)
    tiles.append((name.replace("_", " "), c))

try:
    font = ImageFont.truetype("DejaVuSans.ttf", 24)
except OSError:
    font = ImageFont.load_default()

def sheet(items, cols, cell, out):
    w, h = cell
    rows = (len(items) + cols - 1) // cols
    s = Image.new("RGB", (cols * (w + 24) + 24, rows * (h + 64) + 24), (24, 22, 20))
    d = ImageDraw.Draw(s)
    for i, (label, im) in enumerate(items):
        x, y = 24 + (i % cols) * (w + 24), 24 + (i // cols) * (h + 64)
        t = im.copy()
        t.thumbnail((w, h), Image.LANCZOS)
        s.paste(t, (x + (w - t.width) // 2, y + 40 + (h - t.height) // 2))
        d.text((x, y + 6), label, fill=(232, 228, 220), font=font)
    s.save(out, quality=88)

items = [(LABELS[n], Image.open(os.path.join(review, n + ".jpg"))) for n in LABELS]
chk = Image.new("RGBA", alpha.size, (236, 233, 227, 255))
chk.alpha_composite(alpha)
items.append(("08 Transparent product (on ivory)", chk.convert("RGB")))
sheet(items, 3, (760, 560), os.path.join(dst, "review", "contact_sheet_pass2.jpg"))
sheet([(f"100 %: {l}", c) for l, c in tiles], 4, (520, 400), os.path.join(dst, "review", "detail_crops_pass2.jpg"))
print("ok", len(items), "renders,", len(tiles), "crops")
