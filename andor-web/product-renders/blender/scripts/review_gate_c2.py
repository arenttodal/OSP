"""Gate C, checkpoint C2 review exports (Pillow).

  python3 product-renders/blender/scripts/review_gate_c2.py <C2 render dir> <C1 render dir> <review dir>

Writes the C2 images as JPEGs; typography layers (separate transparent PNGs, the site's own
logotype: ANDOR in Inter Tight 760 with 0.02 em tracking, a muted 300-weight slash, OSP in 300;
the line in EB Garamond) with the measured WCAG contrast of every line against the pixels behind
it (type_contrast.json); and the comparison sheets: before/after (C1 -> C2), B2 scale studies,
C1 cinematic vs sharp, PITCH/MOD flat vs physical (test only), and 00_contact_sheet.jpg.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont, ImageStat

c2, c1, review = sys.argv[1:4]
os.makedirs(review, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "..", "config", "campaign_setups.json")))["setups"]
NM = os.path.join(HERE, "..", "..", "..", "node_modules")
SANS = os.path.join(NM, "@fontsource-variable/inter-tight/files/inter-tight-latin-wght-normal.woff2")
SERIF = os.path.join(NM, "@fontsource/eb-garamond/files/eb-garamond-latin-400-normal.woff2")
BG, FG, MUTED, GAP = (24, 22, 20), (232, 228, 220), (190, 184, 174), 24
# site tokens (shared/tokens/tokens.css)
TEXT, TEXT2, MUTE = (0x24, 0x27, 0x23), (0x5c, 0x63, 0x5a), (0x65, 0x6c, 0x63)
LIGHT_INK, LIGHT_SOFT, LIGHT_MUTE = (0xf4, 0xf0, 0xe8), (0xd6, 0xce, 0xc2), (0xa8, 0xa2, 0x9a)

for n in ("A1", "A1M", "A2_master", "B1", "B2", "B2_115", "B2_125", "C1S", "wheels_flat", "wheels_physical"):
    Image.open(os.path.join(c2, n + ".png")).convert("RGB").save(os.path.join(review, n + ".jpg"), quality=94, subsampling=0)


def lum(rgb):
    def ch(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(x) for x in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def sans(size, weight):
    f = ImageFont.truetype(SANS, size)
    f.set_variation_by_axes([weight])
    return f


def tracked(d, xy, text, font, fill, tracking):
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=font, fill=fill)
        x += font.getlength(ch) + tracking
    return x


report = {}


def type_layer(name, shot, setup, dark):
    im = Image.open(os.path.join(c2, name + ".png")).convert("RGB")
    W, H = im.size
    box = CFG[setup]["shots"][shot]["text_safe"]
    x0, x1, y0 = box["x"][0] * W, box["x"][1] * W, box["y"][0] * H
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    hsize = int((x1 - x0) * (0.105 if W > H else 0.085))
    bold, light = sans(hsize, 760), sans(hsize, 300)
    lines = []
    behind = ImageStat.Stat(im.crop((int(x0), int(y0), int(x1), int(y0 + hsize * 4.2)))).mean[:3]
    ink = LIGHT_INK if dark else TEXT
    slash = LIGHT_MUTE if dark else MUTE
    x = tracked(d, (x0, y0), "ANDOR", bold, ink, hsize * 0.02)
    x += hsize * 0.12
    x = tracked(d, (x, y0), "/", light, slash, 0)
    x += hsize * 0.12
    tracked(d, (x, y0), "OSP", light, ink, 0)
    lines.append(("ANDOR/OSP", ink))
    lsize = int(hsize * 0.78)
    serif = ImageFont.truetype(SERIF, lsize)
    soft = LIGHT_SOFT if dark else TEXT2
    if contrast(soft, behind) < 4.5:      # the secondary colour only where it reads
        soft = LIGHT_INK if dark else TEXT
    y = y0 + hsize * 1.55
    for line in ("One sound.", "An entire instrument."):
        d.text((x0, y), line, font=serif, fill=soft)
        lines.append((line, soft))
        y += lsize * 1.22
    layer.save(os.path.join(review, f"{name}_type_layer.png"), optimize=True)
    comp = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    comp.save(os.path.join(review, f"{name}_type_preview.jpg"), quality=92)
    report[name] = {"background_mean_rgb": [round(v) for v in behind],
                    "lines": [{"text": t, "rgb": list(c), "contrast": round(contrast(c, behind), 2)} for t, c in lines]}
    return comp


tA1 = type_layer("A1", "A1", "ANDOR_GALLERY", False)
tA1M = type_layer("A1M", "A1M", "ANDOR_GALLERY", False)
tB2 = type_layer("B2_115", "B2_115", "ANDOR_FLOATING", True)
json.dump(report, open(os.path.join(review, "type_contrast.json"), "w"), indent=1)

try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()


def load(d, n):
    return Image.open(os.path.join(d, n + ".png")).convert("RGB")


def sheet(rows, title, path, height=720):
    built = [(label, [(c, im.resize((round(im.width * height / im.height), height), Image.LANCZOS)) for c, im in items]) for label, items in rows]
    W = max(GAP + sum(im.width + GAP for _, im in ims) for _, ims in built)
    H = GAP + 56 + len(built) * (GAP + 40 + height + 34)
    s = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(s)
    d.text((GAP, GAP), title, fill=FG, font=font)
    y = GAP + 56
    for label, ims in built:
        d.text((GAP, y + GAP), label, fill=FG, font=small)
        y += GAP + 40
        x = GAP
        for cap, im in ims:
            s.paste(im, (x, y))
            d.text((x, y + height + 6), cap, fill=MUTED, font=small)
            x += im.width + GAP
        y += height + 34
    s.save(path, quality=90)


sheet([("A1 · desktop hero", [("C1", load(c1, "A1")), ("C2: warmer stone, low rake on the controls and keys", load(c2, "A1"))]),
       ("A2 · product gallery", [("C1 (56 spp)", load(c1, "A2")), ("C2 master: 3600 x 2400, 160 spp, noise 0.005", load(c2, "A2_master"))]),
       ("B1 · dark advertisement", [("C1", load(c1, "B1")), ("C2: product lights -0.3 EV, floor unchanged", load(c2, "B1"))]),
       ("C1 · material macro", [("cinematic (f/8, C1)", load(c1, "C1")), ("sharp (f/14, 0.20 m)", load(c2, "C1S"))])],
      "Gate C2 · before / after (same cameras)", os.path.join(review, "07_before_after.jpg"))
sheet([("B2 · projected size, recomposed with the copy area (top 14-34 %) and the bottom 20 % kept clear",
        [("current", load(c2, "B2")), ("+15 % (recommended)", load(c2, "B2_115")), ("+25 % (recomposed, -34 deg): still touches a cheek", load(c2, "B2_125")),
         ("+15 % with type layer", tB2)])],
      "Gate C2 · B2 vertical scale studies", os.path.join(review, "05_B2_scale_studies.jpg"), 900)
sheet([("A1 mobile: its own composition (not a crop)", [("A1M", load(c2, "A1M")), ("A1M with type layer", tA1M)]),
       ("A1 desktop with type layer", [("A1 + ANDOR/OSP logotype", tA1)])],
      "Gate C2 · A1 desktop and mobile", os.path.join(review, "02_A1_desktop_mobile.jpg"), 900)
sheet([("PITCH / MOD · isolated test (not used in any campaign image)", [("flat artwork (current)", load(c2, "wheels_flat")),
                                                                        ("physical: bezel, slot, wheel, PITCH band", load(c2, "wheels_physical"))])],
      "Gate C2 · PITCH/MOD physical modelling test", os.path.join(review, "08_pitch_mod_test.jpg"), 800)
items = [("A1 desktop", load(c2, "A1")), ("A1M mobile", load(c2, "A1M")), ("A2 master", load(c2, "A2_master")),
         ("B1", load(c2, "B1")), ("B2 +15 %", load(c2, "B2_115")), ("C1 sharp", load(c2, "C1S"))]
sheet([("Gate C2 · refined compositions", items[:3]), ("", items[3:])], "ANDOR OSP · Gate C2 contact sheet", os.path.join(review, "00_contact_sheet.jpg"), 640)
print("ok", json.dumps(report))
