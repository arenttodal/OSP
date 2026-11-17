"""Gate C2.5 review exports (Pillow): drafts, typography and layout previews, comparison sheets.

  python3 product-renders/blender/scripts/review_gate_c25.py <C2.5 draft dir> <C2 render dir> <current capture dir> <review dir> [<touch strip dir>]

Typography stays live in Framer: every preview here is a layer over a clean product-only image
(the masters carry no type). Copy is the approved headline and the site's existing nav, CTA and
lines (prototype/osp); nothing new is claimed.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont, ImageStat

d25, c2, cur, review = sys.argv[1:5]
os.makedirs(review, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
NM = os.path.join(HERE, "..", "..", "..", "node_modules")
SANS = os.path.join(NM, "@fontsource-variable/inter-tight/files/inter-tight-latin-wght-normal.woff2")
SERIF = os.path.join(NM, "@fontsource/eb-garamond/files/eb-garamond-latin-400-normal.woff2")
BG, FG, MUTED, GAP = (24, 22, 20), (232, 228, 220), (190, 184, 174), 24
TEXT, TEXT2, MUTE = (0x24, 0x27, 0x23), (0x5c, 0x63, 0x5a), (0x65, 0x6c, 0x63)
LIGHT_INK, LIGHT_SOFT, LIGHT_MUTE = (0xf4, 0xf0, 0xe8), (0xd6, 0xce, 0xc2), (0xa8, 0xa2, 0x9a)
HEAD = ("One sound.", "An entire instrument.")
SUPPORT = "One recording is enough to begin."          # prototype/osp/content.ts (layers, 1)
NAV = ("Instrument", "Sounds", "Questions")             # prototype/osp/main.tsx
CTA, CTA2 = "Join the waitlist", "Hear it"              # main.tsx header CTA, Hero.tsx link

try:
    font = ImageFont.truetype("DejaVuSans.ttf", 26)
    small = ImageFont.truetype("DejaVuSans.ttf", 20)
except OSError:
    font = small = ImageFont.load_default()


def sans(size, weight):
    f = ImageFont.truetype(SANS, max(8, int(size)))
    f.set_variation_by_axes([weight])
    return f


def serif(size):
    return ImageFont.truetype(SERIF, max(8, int(size)))


def lum(rgb):
    def ch(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(x) for x in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def load(d, n):
    return Image.open(os.path.join(d, n + ".png")).convert("RGB")


def tracked(d, xy, text, f, fill, tracking):
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill)
        x += f.getlength(ch) + tracking
    return x


def wordmark(d, xy, size, ink, slash):
    x = tracked(d, xy, "ANDOR", sans(size, 760), ink, size * 0.02)
    x = tracked(d, (x + size * 0.12, xy[1]), "/", sans(size, 300), slash, 0)
    return tracked(d, (x + size * 0.12, xy[1]), "OSP", sans(size, 300), ink, 0)


def pill(d, box, fill, outline, label, f, ink):
    d.rounded_rectangle(box, radius=(box[3] - box[1]) / 2, fill=fill, outline=outline, width=max(1, int((box[3] - box[1]) / 22)))
    w = f.getlength(label)
    d.text(((box[0] + box[2] - w) / 2, (box[1] + box[3]) / 2), label, font=f, fill=ink, anchor="lm")


report = {}


def arrow_link(d, x, y, label, f, ink, size):
    """A text link with a drawn arrow (the web fonts carry no arrow glyph)."""
    d.text((x, y), label, font=f, fill=ink, anchor="lm")
    a = x + f.getlength(label) + size * 0.5
    w = max(1, int(size * 0.09))
    d.line((a, y, a + size * 0.9, y), fill=ink, width=w)
    d.line((a + size * 0.55, y - size * 0.32, a + size * 0.9, y), fill=ink, width=w)
    d.line((a + size * 0.55, y + size * 0.32, a + size * 0.9, y), fill=ink, width=w)


def checked(name, im, box, colour, alt):
    """The colour if it reaches 4.5:1 on the pixels behind the box, else the alternative."""
    behind = ImageStat.Stat(im.crop(tuple(int(v) for v in box))).mean[:3]
    c = colour if contrast(colour, behind) >= 4.5 else alt
    report.setdefault(name, []).append({"box": [int(v) for v in box], "rgb": list(c), "contrast": round(contrast(c, behind), 2),
                                        "background_mean_rgb": [round(v) for v in behind]})
    return c


def desktop_preview(name, src):
    """A1 as the website's first screen: nav, a dominant headline, supporting line, CTA. The
    headline is the page's message (about 6.5 % of the width per line), not a caption."""
    im = src.copy()
    W, H = im.size
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    m = W * 0.055
    nav = H * 0.034
    wordmark(d, (m, H * 0.05), nav * 1.15, TEXT, MUTE)
    x = W - m
    pill_f = sans(nav * 0.82, 560)
    cw = pill_f.getlength(CTA) + nav * 2.0
    pill(d, (x - cw, H * 0.04, x, H * 0.04 + nav * 2.0), TEXT, TEXT, CTA, pill_f, LIGHT_INK)
    x -= cw + nav * 1.6
    for item in reversed(NAV):
        f = sans(nav * 0.86, 500)
        x -= f.getlength(item)
        d.text((x, H * 0.04 + nav), item, font=f, fill=TEXT, anchor="lm")
        x -= nav * 1.6
    safe = CFG["ANDOR_GALLERY"]["shots"]["A1"]["text_safe"]
    span = min((safe["x"][1] - safe["x"][0]) * W, 0.29 * W)   # measured: the synced instrument's left corner sits at 0.33 W at headline height
    hs = W * 0.062
    while serif(hs).getlength(HEAD[1]) > span and hs > 10:      # the headline fills the copy area, never the product
        hs *= 0.97
    y = H * 0.30
    for i, line in enumerate(HEAD):
        box = (m, y, m + W * 0.4, y + hs * 1.1)
        c = checked(name, im, box, TEXT if i == 0 else TEXT2, TEXT)
        d.text((m, y), line, font=serif(hs), fill=c)
        y += hs * 1.08
    y += hs * 0.45
    ss = W * 0.0165
    d.text((m, y), SUPPORT, font=sans(ss, 420), fill=checked(name, im, (m, y, m + W * 0.3, y + ss * 1.3), TEXT2, TEXT))
    y += ss * 2.6
    f = sans(ss * 0.95, 560)
    cw = f.getlength(CTA) + ss * 2.4
    pill(d, (m, y, m + cw, y + ss * 2.6), TEXT, TEXT, CTA, f, LIGHT_INK)
    arrow_link(d, m + cw + ss * 1.4, y + ss * 1.3, CTA2, f, TEXT, ss)
    layer.save(os.path.join(review, f"{name}_type_layer.png"), optimize=True)
    out = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    out.save(os.path.join(review, f"{name}_web_preview.jpg"), quality=92)
    return out


def mobile_preview(name, src, css_w, css_h, layout):
    """The phone's first screen at 3x: the product-only master fills the width (object-fit:
    cover, top aligned); nav, headline and CTA are live layers at the layout's fractions."""
    S = 3
    W, H = css_w * S, css_h * S
    im = src.resize((W, round(src.height * W / src.width)), Image.LANCZOS)
    if im.height < H:     # (never for the 19.5:9 master; a 9:16 one would need the floor colour below)
        pad = Image.new("RGB", (W, H), im.getpixel((W // 2, im.height - 1)))
        pad.paste(im, (0, 0))
        im = pad
    im = im.crop((0, 0, W, H))
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    m = 20 * S
    wordmark(d, (m, H * layout["nav"][0] + 18 * S), 17 * S, TEXT, MUTE)
    yb = H * layout["nav"][0] + 21 * S
    for i in range(3):     # the menu button: three lines (drawn: the web fonts carry no such glyph)
        d.line((W - m - 20 * S, yb + i * 6 * S, W - m, yb + i * 6 * S), fill=TEXT, width=2 * S)
    hs = 40 * S
    y = H * layout["headline"][0]
    for i, line in enumerate(HEAD):
        c = checked(name, im, (m, y, W - m, y + hs), TEXT if i == 0 else TEXT2, TEXT)
        d.text((m, y), line, font=serif(hs), fill=c)
        y += hs * 1.08
    d.text((m, y + 10 * S), SUPPORT, font=sans(15 * S, 420), fill=checked(name, im, (m, y, W - m, y + 30 * S), TEXT2, TEXT))
    y = H * layout["cta"][0]
    f = sans(16 * S, 560)
    pill(d, (m, y, W - m, y + 50 * S), TEXT, TEXT, CTA, f, LIGHT_INK)
    arrow_link(d, W / 2 - (f.getlength(CTA2) + 22 * S) / 2, y + 50 * S + 26 * S, CTA2, f, TEXT, 15 * S)
    out = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    out.save(os.path.join(review, f"{name}_{css_w}x{css_h}.jpg"), quality=92)
    return out


def vertical_ad(name, src, text_safe):
    im = src.copy()
    W, H = im.size
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0 = text_safe["x"][0] * W
    y = text_safe["y"][0] * H
    hs = W * 0.072
    wordmark(d, (x0, y), hs * 0.62, LIGHT_INK, LIGHT_MUTE)
    y += hs * 1.2
    for i, line in enumerate(HEAD):
        c = checked(name, im, (x0, y, W * 0.92, y + hs), LIGHT_INK if i == 0 else LIGHT_SOFT, LIGHT_INK)
        d.text((x0, y), line, font=serif(hs), fill=c)
        y += hs * 1.1
    f = sans(W * 0.034, 560)
    cy = H * 0.83
    pill(d, (W * 0.08, cy, W * 0.92, cy + W * 0.1), LIGHT_INK, LIGHT_INK, CTA, f, TEXT)
    layer.save(os.path.join(review, f"{name}_type_layer.png"), optimize=True)
    out = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    out.save(os.path.join(review, f"{name}_type_preview.jpg"), quality=92)
    return out


def sheet(rows, title, path, height=720):
    built = [(label, [(c, im.resize((round(im.width * height / im.height), height), Image.LANCZOS)) for c, im in items]) for label, items in rows]
    Wd = max(GAP + sum(im.width + GAP for _, im in ims) for _, ims in built)
    Hd = GAP + 56 + len(built) * (GAP + 40 + height + 34)
    s = Image.new("RGB", (Wd, Hd), BG)
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


def crop(im, f):
    W, H = im.size
    return im.crop((int(f[0] * W), int(f[1] * H), int(f[2] * W), int(f[3] * H)))


CFG = json.load(open(os.path.join(HERE, "..", "config", "campaign_setups.json")))["setups"]
for n in ("A1_sync", "A1_R1", "A1_R2", "A1M_R1", "A2", "B1_R0", "B1_R1", "B1_R2", "B1_R3", "B1_R4", "B2_115_R4", "M02", "M04"):
    if os.path.exists(os.path.join(d25, n + ".png")):
        load(d25, n).save(os.path.join(review, n + ".jpg"), quality=93, subsampling=0)

# 01 A1 light environments
A = [load(c2, "A1"), load(d25, "A1_sync"), load(d25, "A1_R1"), load(d25, "A1_R2")]
CTRL = (0.42, 0.42, 0.78, 0.72)
sheet([("A1 · light environment (drafts; camera, composition and product unchanged)",
        [("C2 (previous GUI)", A[0]), ("C2 light, current GUI", A[1]), ("A1-R1 Warm Gallery", A[2]), ("A1-R2 Architectural Ivory", A[3])]),
       ("Controls at 100 % of the draft: fluting, small-knob bevels, card edges, key fronts",
        [("C2 light", crop(A[1], CTRL)), ("R1", crop(A[2], CTRL)), ("R2", crop(A[3], CTRL))])],
      "Gate C2.5 · A1 desktop hero: two refined environments", os.path.join(review, "01_A1_light.jpg"), 560)
# 02 A1 website composition
desktop_preview("A1_R1", A[2])
desktop_preview("A1_R2", A[3])
sheet([("The headline as the page's message (live type in Framer; the master stays clean)",
        [("A1-R1 + site layout", Image.open(os.path.join(review, "A1_R1_web_preview.jpg"))),
         ("A1-R2 + site layout", Image.open(os.path.join(review, "A1_R2_web_preview.jpg")))])],
      "Gate C2.5 · A1 website composition preview", os.path.join(review, "02_A1_web_preview.jpg"), 640)
# 03 A1M mobile
lay = CFG["ANDOR_GALLERY"]["shots"]["A1M_R1"]["layout"]
m = load(d25, "A1M_R1")
p1 = mobile_preview("A1M_R1", m, 390, 844, lay)
p2 = mobile_preview("A1M_R1", m, 430, 932, lay)
sheet([("A1M-R1: product-only master (19.5:9, product right under the headline) and the phone's first screen",
        [("C2 A1M (9:16)", load(c2, "A1M")), ("A1M-R1 master", m), ("390 x 844 CSS", p1), ("430 x 932 CSS", p2)])],
      "Gate C2.5 · A1 mobile: the product closer to the headline", os.path.join(review, "03_A1M_mobile.jpg"), 900)
# 04 B1 light ratio
B = [load(d25, n) for n in ("B1_R0", "B1_R1", "B1_R2", "B1_R3", "B1_R4")]
BC = (0.12, 0.40, 0.62, 0.62)
sheet([("B1 · fill against directional light (drafts)", [("R0 (C2)", B[0]), ("R1 fill -15 %", B[1]), ("R2 fill -25 %", B[2]),
                                                       ("R3 fill + top line -25 %", B[3]), ("R4 R3 + smaller key (recommended)", B[4])]),
       ("Controls at 2x of the draft", [("R0", crop(B[0], BC)), ("R3", crop(B[3], BC)), ("R4", crop(B[4], BC))])],
      "Gate C2.5 · B1 dark flagship: the light ratio", os.path.join(review, "04_B1_light_ratio.jpg"), 620)
# 05 B2 +15 % locked
b2 = load(d25, "B2_115_R4")
t = vertical_ad("B2_115_R4", b2, CFG["ANDOR_FLOATING"]["shots"]["B2_115"]["text_safe"])
sheet([("B2 +15 % (locked) with B1-R4's light: product-only and type preview (copy 14-34 %, CTA in the bottom 20 %)",
        [("C2 B2 +15 %", load(c2, "B2_115")), ("C2.5 product-only", b2), ("C2.5 + type", t)])],
      "Gate C2.5 · B2 vertical", os.path.join(review, "05_B2_vertical.jpg"), 900)
# 06 PITCH / MOD touch strips (test only: not merged into the master until approved)
strip = sys.argv[5] if len(sys.argv) > 5 else None
if strip and os.path.exists(os.path.join(strip, "4_strip_B_crown2.png")):
    L = lambda n: load(strip, n)
    sheet([("Same light and camera, no depth of field", [("1 · previous: mechanical wheel (rejected)", L("1_mechanical_wheel")), ("2 · current flat GUI artwork", L("2_flat_gui")),
                                                       ("3 · touch strip A: flat", L("3_strip_A_flat"))]),
           ("", [("4 · touch strip B: 2 % crown (recommended), bezel 0.4 mm", L("4_strip_B_crown2")), ("5 · touch strip C: 4 % crown", L("5_strip_C_crown4")),
                 ("B with the 0.3 mm bezel", L("4b_strip_B_bezel03"))])],
          "Gate C2.5 · PITCH/MOD as full-height touch strips", os.path.join(review, "06_touch_strips.jpg"), 520)
    gui = Image.open(os.path.join(cur, "cur-hero-3L@4x.png")).convert("RGB")
    gui = gui.crop((11 * 4, (957 - 64 - 50) * 4, 161 * 4, (957 - 64 + 50) * 4))
    sheet([("6 · orthographic, straight down, against the editor (same region: editor x 11-161)",
            [("the editor's own pixels", gui), ("flat artwork on the model", L("6_top_flat_gui")), ("touch strip B", L("6_top_strip_B"))]),
           ("7 · sharp three-quarter macro and 8 · the instrument with strip B",
            [("7 · strip B, 3/4 macro", L("7_macro34_strip_B")), ("8 · neutral, strip B", L("8_neutral_strip_B"))])],
          "Gate C2.5 · touch strips: plan check, macro, whole instrument", os.path.join(review, "06b_touch_strips_check.jpg"), 520)
    for n in ("1_mechanical_wheel", "2_flat_gui", "3_strip_A_flat", "4_strip_B_crown2", "5_strip_C_crown4", "4b_strip_B_bezel03",
              "6_top_strip_B", "6_top_flat_gui", "7_macro34_strip_B", "8_neutral_strip_B"):
        L(n).save(os.path.join(review, f"strip_{n}.jpg"), quality=93, subsampling=0)
if os.path.exists(os.path.join(c2, "wheels_physical.png")) and os.path.exists(os.path.join(d25, "wheels_corner_satin_04.png")):
    sheet([("Left timber junction: C2 (dark sliver: slot material on the panel's side wall) vs fixed",
            [("C2", crop(load(c2, "wheels_physical"), (0.25, 0.2, 0.55, 0.47))), ("C2.5", crop(load(d25, "wheels_corner_satin_04"), (0.25, 0.2, 0.55, 0.47)))])],
          "Gate C2.5 · timber junction", os.path.join(review, "06c_timber_junction.jpg"), 520)
# 07 GUI sync
capt = Image.open(os.path.join(cur, "cur-hero-3L@2x.png")).convert("RGB")
sheet([("The marketing model against the current editor (OSP default branch 08ebd01)",
        [("C2 A2 (model from the 9 Nov editor)", load(c2, "A2_master")), ("Current editor, 3 layers", capt), ("C2.5 A2 (model synced)", load(d25, "A2"))])],
      "Gate C2.5 · GUI vs marketing model", os.path.join(review, "07_gui_sync.jpg"), 640)
# 08 details
sheet([("Material details (drafts; the MATERIAL setup's light, f/11)", [("M02 · source controls", load(d25, "M02")), ("M04 · keyboard", load(d25, "M04"))])],
      "Gate C2.5 · detail library previews", os.path.join(review, "08_details.jpg"), 640)
# 00 contact sheet
sheet([("A1 desktop", [("R1 Warm Gallery", A[2]), ("R2 Architectural Ivory", A[3])]),
       ("Mobile, B1, B2, details", [("A1M-R1", m), ("B1-R4", B[4]), ("B2 +15 %", b2), ("M02", load(d25, "M02")), ("M04", load(d25, "M04"))])],
      "ANDOR OSP · Gate C2.5 contact sheet (drafts)", os.path.join(review, "00_contact_sheet.jpg"), 520)
json.dump(report, open(os.path.join(review, "type_contrast.json"), "w"), indent=1)
print("ok", {k: [l["contrast"] for l in v] for k, v in report.items()})
