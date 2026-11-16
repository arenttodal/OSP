"""Gate C, checkpoint C1 review exports (Pillow).

  python3 product-renders/blender/scripts/review_gate_c.py <render dir> <study dir> <review dir>

<render dir>  A1, A2, B1, B2, C1 .png from andor_scene.py --setup ... --shot ...
<study dir>   scale_{100,115,125}.png and float_{050,100,180}.png (ANDOR_FLOATING B1 drafts)
Writes: full-resolution JPEGs of the five; typography layers as separate transparent PNGs (A1, B2)
plus a preview of each over its render (the masters are never touched); the B scale / float
studies; and 00_contact_sheet.jpg. Type is the site's own: Inter Tight (headline), EB Garamond (line).
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

src, study, review = sys.argv[1:4]
os.makedirs(review, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "..", "config", "campaign_setups.json")))["setups"]
NM = os.path.join(HERE, "..", "..", "..", "node_modules")
SANS = os.path.join(NM, "@fontsource-variable/inter-tight/files/inter-tight-latin-wght-normal.woff2")
SERIF = os.path.join(NM, "@fontsource/eb-garamond/files/eb-garamond-latin-400-normal.woff2")
BG, FG, MUTED, GAP = (24, 22, 20), (232, 228, 220), (190, 184, 174), 24

SHOTS = [("A1", "ANDOR_GALLERY"), ("A2", "ANDOR_GALLERY"), ("B1", "ANDOR_FLOATING"), ("B2", "ANDOR_FLOATING"), ("C1", "ANDOR_MATERIAL")]
for shot, _ in SHOTS:
    Image.open(os.path.join(src, shot + ".png")).convert("RGB").save(os.path.join(review, f"{shot}.jpg"), quality=94, subsampling=0)


def sans(size, weight=560):
    f = ImageFont.truetype(SANS, size)
    try:
        f.set_variation_by_axes([weight])
    except (OSError, AttributeError):
        pass
    return f


def type_layer(shot, setup, dark):
    """The copy as its own transparent layer, set inside the shot's text-safe box."""
    im = Image.open(os.path.join(src, shot + ".png"))
    W, H = im.size
    box = CFG[setup]["shots"][shot]["text_safe"]
    x0, x1 = box["x"][0] * W, box["x"][1] * W
    y0 = box["y"][0] * H
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    ink = (244, 240, 232, 255) if dark else (38, 35, 31, 255)
    soft = (214, 206, 194, 255) if dark else (92, 86, 78, 255)
    width = x1 - x0
    hsize = int(width * (0.105 if shot == "A1" else 0.085))
    d.text((x0, y0), "ANDOR OSP", font=sans(hsize, 600), fill=ink)
    lsize = int(hsize * 0.78)
    serif = ImageFont.truetype(SERIF, lsize)
    y = y0 + hsize * 1.55
    for line in ("One sound.", "An entire instrument."):
        d.text((x0, y), line, font=serif, fill=soft)
        y += lsize * 1.22
    layer.save(os.path.join(review, f"{shot}_type_layer.png"), optimize=True)
    comp = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    comp.save(os.path.join(review, f"{shot}_type_preview.jpg"), quality=92)
    return comp


type_a1 = type_layer("A1", "ANDOR_GALLERY", dark=False)
type_b2 = type_layer("B2", "ANDOR_FLOATING", dark=True)

try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()


def row_sheet(rows, title, path, height=820):
    """rows: [(label, [(caption, image)])]; images scaled to one height."""
    built = []
    for label, items in rows:
        ims = [(c, im.resize((round(im.width * height / im.height), height), Image.LANCZOS)) for c, im in items]
        built.append((label, ims))
    W = max(GAP + sum(im.width + GAP for _, im in ims) for _, ims in built)
    H = GAP + 56 + sum(GAP + 40 + height + 34 for _ in built)
    sheet = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(sheet)
    d.text((GAP, GAP), title, fill=FG, font=font)
    y = GAP + 56
    for label, ims in built:
        d.text((GAP, y + GAP), label, fill=FG, font=small)
        y += GAP + 40
        x = GAP
        for cap, im in ims:
            sheet.paste(im, (x, y))
            d.text((x, y + height + 6), cap, fill=MUTED, font=small)
            x += im.width + GAP
        y += height + 34
    sheet.save(path, quality=90)


def load(d, n):
    return Image.open(os.path.join(d, n + ".png")).convert("RGB")


row_sheet([("Projected product size (camera distance only; geometry unchanged)",
            [("baseline framing", load(study, "scale_100")), ("+15 % (chosen)", load(study, "scale_115")), ("+25 %", load(study, "scale_125"))]),
           ("Floating height (product to floor), at +15 %",
            [("50 mm: reads as resting", load(study, "float_050")), ("100 mm (chosen)", load(study, "float_100")), ("180 mm: detached", load(study, "float_180"))])],
          "Direction B studies (drafts; superseded camera height, see B1 for the final)", os.path.join(review, "B_scale_float_studies.jpg"), 700)

row_sheet([("Direction A · The Gallery", [(CFG["ANDOR_GALLERY"]["shots"]["A1"]["title"], load(src, "A1")),
                                         (CFG["ANDOR_GALLERY"]["shots"]["A2"]["title"], load(src, "A2"))]),
           ("Direction B · The Floating Instrument  ·  Direction C · The Material Study",
            [("B1 · cinematic still (4:5)", load(src, "B1")),
             ("B2 · type-safe (9:16)", load(src, "B2")),
             ("C1 · macro knobs (1:1)", load(src, "C1"))]),
           ("Typography as separate layers (not in the masters)", [("A1 + type layer", type_a1), ("B2 + type layer", type_b2)])],
          "ANDOR OSP · Gate C · Checkpoint C1: art direction", os.path.join(review, "00_contact_sheet.jpg"), 820)
print("ok", review)
