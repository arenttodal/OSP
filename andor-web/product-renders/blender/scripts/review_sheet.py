"""Review contact sheet for a render pass (Pillow): the hero, the transparent render on a checker,
the type mock with safe-zone guides, and two 100 % crops. Also prints a colour check of flat
interface areas (render vs capture).

  python3 product-renders/blender/scripts/review_sheet.py <hero.png> <alpha.png> <ad_guides.png> <out.png>
"""
import sys

from PIL import Image, ImageDraw, ImageFont

hero, alpha, ad, out = sys.argv[1:5]
H = Image.open(hero).convert("RGB")
A = Image.open(alpha).convert("RGBA")
G = Image.open(ad).convert("RGB")

col_h = 1100
def fit(im, h=col_h):
    return im.resize((round(im.width * h / im.height), h), Image.LANCZOS)

checker = Image.new("RGBA", A.size, (0, 0, 0, 0))
d = ImageDraw.Draw(checker)
s = 40
for y in range(0, A.height, s):
    for x in range(0, A.width, s):
        d.rectangle((x, y, x + s, y + s), fill=(205, 205, 205, 255) if (x // s + y // s) % 2 else (240, 240, 240, 255))
checker.alpha_composite(A)

# crops at 100 %: the knob area and a display + cheek, located from the alpha bounding box
bx0, by0, bx1, by1 = A.getbbox()
cw, ch = 520, 520
c1 = H.crop((bx0 + int((bx1 - bx0) * 0.12), by0 + int((by1 - by0) * 0.55), bx0 + int((bx1 - bx0) * 0.12) + cw, by0 + int((by1 - by0) * 0.55) + ch))
c2 = H.crop((bx1 - cw, by0, bx1, by0 + ch))

tiles = [fit(H), fit(checker.convert("RGB")), fit(G), c1.resize((col_h // 2, col_h // 2)), c2.resize((col_h // 2, col_h // 2))]
W = sum(t.width for t in tiles[:3]) + col_h // 2 + 5 * 24
sheet = Image.new("RGB", (W, col_h + 90), (24, 22, 20))
x = 24
labels = ["01 Hero 9:16 (combined)", "Transparent product", "Type mock + Stories safe zones", "100 % crops"]
dr = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 22)
except OSError:
    font = ImageFont.load_default()
for i, t in enumerate(tiles[:3]):
    sheet.paste(t, (x, 60))
    dr.text((x, 22), labels[i], fill=(230, 226, 218), font=font)
    x += t.width + 24
sheet.paste(tiles[3], (x, 60))
sheet.paste(tiles[4], (x, 60 + col_h // 2))
dr.text((x, 22), labels[3], fill=(230, 226, 218), font=font)
sheet.save(out)
print("wrote", out, sheet.size)
