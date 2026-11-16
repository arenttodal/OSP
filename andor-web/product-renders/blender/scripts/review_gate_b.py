"""Gate B (neutral 3D master) review exports (Pillow).

  python3 product-renders/blender/scripts/review_gate_b.py <out dir> <cmp dir> <r2 dir> <review dir>

<out dir>  01..08 renders (andor_scene.py defaults: --finish refined --teak warm --light sculpt)
<cmp dir>  material comparisons, all under the Gate B rig and the same cameras:
           wood_{r2,current,neutral,warm}.png (corner_fl), ceramic_{r2,refined}.png (macros)
<r2 dir>   the approved keyboard + front body round (neutral rig, r2 materials) for before/after
Writes full-resolution JPEGs 01-08, 07_material_comparison.jpg, 09_before_after.jpg and
00_contact_sheet.jpg.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

out, cmp_, r2, review = sys.argv[1:5]
os.makedirs(review, exist_ok=True)
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()
BG, FG, MUTED, GAP = (24, 22, 20), (232, 228, 220), (190, 184, 174), 24

VIEWS = [
    ("01_camera_A", "01 · Full product, camera A"),
    ("02_camera_B", "02 · Full product, camera B"),
    ("03_keyboard_macro", "03 · Keyboard macro"),
    ("04_left_front_corner", "04 · Left-front corner: wood, ivory, PITCH/MOD, keybed"),
    ("05_right_front_corner", "05 · Right-front corner: ARP, front return, timber"),
    ("06_macro_knob", "06 · Macro knob (G, 0.30 D)"),
    ("08_low_front", "08 · Low front view: physical thickness"),
]
for name, _ in VIEWS:
    Image.open(os.path.join(out, name + ".png")).convert("RGB").save(os.path.join(review, name + ".jpg"), quality=94, subsampling=0)


def grid(rows, cell_w, title, path):
    """rows: [(row label, [(caption, image), ...])]"""
    cols = max(len(r[1]) for r in rows)
    cells = []
    for label, items in rows:
        h = round(cell_w * items[0][1].height / items[0][1].width)
        cells.append((label, [(c, im.resize((cell_w, h), Image.LANCZOS)) for c, im in items], h))
    H = GAP + 56 + sum(GAP + 40 + h + 34 for _, _, h in cells)
    sheet = Image.new("RGB", (GAP + cols * (cell_w + GAP), H), BG)
    d = ImageDraw.Draw(sheet)
    d.text((GAP, GAP), title, fill=FG, font=font)
    y = GAP + 56
    for label, items, h in cells:
        d.text((GAP, y + GAP), label, fill=FG, font=small)
        y += GAP + 40
        for k, (cap, im) in enumerate(items):
            x = GAP + k * (cell_w + GAP)
            sheet.paste(im, (x, y))
            d.text((x, y + h + 6), cap, fill=MUTED, font=small)
        y += h + 34
    sheet.save(path, quality=91)


def load(d, n):
    return Image.open(os.path.join(d, n + ".png")).convert("RGB")


grid([("Wood (left-front corner)", [("r2 teak (approved round)", load(cmp_, "wood_r2")),
                                    ("current tone + natural grain", load(cmp_, "wood_current")),
                                    ("1 · neutral teak", load(cmp_, "wood_neutral")),
                                    ("2 · deeper warm satin teak (chosen)", load(cmp_, "wood_warm"))]),
      ("Ceramic family (macro area)", [("r2: one ceramic response", load(cmp_, "ceramic_r2")),
                                       ("refined: housing, cards, macro panel, buttons, keycaps", load(cmp_, "ceramic_refined"))])],
     760, "07 · Materials: current vs refined (same geometry, camera and Gate B lighting)",
     os.path.join(review, "07_material_comparison.jpg"))

pairs = [("Camera B", "B", "02_camera_B"), ("Left-front corner", "corner_fl", "04_left_front_corner"),
         ("Right-front corner", "corner", "05_right_front_corner"), ("Low front", "front_edge", "08_low_front")]
grid([(label, [("before: approved keyboard + front body round (neutral rig, r2 materials)", load(r2, a)),
               ("after: Gate B (corrections, materials, studio rig)", load(out, b))]) for label, a, b in pairs],
     1200, "09 · Before / after: same cameras", os.path.join(review, "09_before_after.jpg"))

items = [(cap, Image.open(os.path.join(review, n + ".jpg"))) for n, cap in VIEWS]
items.insert(6, ("07 · Material comparison", Image.open(os.path.join(review, "07_material_comparison.jpg"))))
TW, TH = 900, 675
sheet = Image.new("RGB", (GAP + 4 * (TW + GAP), GAP + 2 * (TH + 50 + GAP)), BG)
d = ImageDraw.Draw(sheet)
for i, (cap, im) in enumerate(items):
    x, y = GAP + (i % 4) * (TW + GAP), GAP + (i // 4) * (TH + 50 + GAP)
    t = im.copy()
    t.thumbnail((TW, TH), Image.LANCZOS)
    sheet.paste(t, (x + (TW - t.width) // 2, y + 44 + (TH - t.height) // 2))
    d.text((x, y + 8), cap.split(":")[0], fill=FG, font=small)
sheet.save(os.path.join(review, "00_contact_sheet.jpg"), quality=88)
print("ok", review)
