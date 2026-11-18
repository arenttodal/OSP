"""ANDOR_Website_Asset_Kit_v1: a curated Framer package from the approved C3 masters.

  python3 product-renders/blender/scripts/website_kit.py <masters dir> <web derivatives dir> <out dir>

Framer accepts JPEG, PNG, WebP and GIF uploads and makes its own AVIF / WebP variants and srcset
on upload, so the kit gives Framer one high-quality source per image (01_framer_upload: lossless
WebP, 8-bit sRGB, alpha kept), never an already lossy file it would compress again. For code
outside Framer (a custom component, an email, a press page), 02_responsive carries a curated set of
the existing AVIF / WebP derivatives at the widths a website uses, copied as they are (no second
compression). 00_index holds a visual index and a usage table.
"""
import json
import os
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

masters, web, out = sys.argv[1:4]
KIT = os.path.join(out, "ANDOR_Website_Asset_Kit_v1")

# (readable kit name, master, intended website use, responsive widths)
ITEMS = [
    ("andor-osp_hero-desktop_a1-r1", "C3_A1_R1_desktop_3840x2160", "Desktop hero, live headline left of the product", [1280, 1920, 2560, 3840]),
    ("andor-osp_hero-desktop-wide_a1-r1-w", "C3_A1_R1_W_desktop_3840x2160", "Desktop hero for the larger headline (product about 6 % right)", [1280, 1920, 2560, 3840]),
    ("andor-osp_hero-mobile_a1m-r1", "C3_A1M_R1_mobile_2160x4674", "Mobile hero (19.5:9; cover-fit, crops top and bottom on 9:16)", [960, 1280, 2160]),
    ("andor-osp_gallery_a2", "C3_A2_gallery_3600x2400", "Gallery / product section", [1280, 1920, 2560, 3600]),
    ("andor-osp_product_transparent", "C3_transparent_product_4200x2800", "Product cut-out over any background (layer 3 of 3)", [1280, 1920, 2560, 3840]),
    ("andor-osp_product-with-shadow_transparent", "C3_transparent_product_shadow_4200x2800", "Product with its floor shadow, over a light background", [1280, 1920, 2560, 3840]),
    ("andor-osp_shadow-only_transparent", "C3_transparent_shadow_only_4200x2800", "Shadow layer under the cut-out (layer 2 of 3)", [1920, 3840]),
    ("andor-osp_background-clean", "C3_background_clean_4200x2800", "Background plate (layer 1 of 3)", [1920, 3840]),
    ("andor-osp_dark-hero_b1-r4", "C3_B1_R4_feed_2160x2700", "Dark section hero / 4:5 social", [960, 1280, 2160]),
    ("andor-osp_dark-vertical_b2", "C3_B2_115_R4_vertical_2160x3840", "Dark vertical (mobile section, stories)", [960, 1280, 2160]),
]
DETAIL = {"M01": "macro-knobs", "M02": "source-controls", "M03": "waveform-display", "M04": "keyboard",
          "M05": "timber-corner", "M06": "touch-strips"}
for m, slug in DETAIL.items():
    for v, word in (("sharp", "sharp"), ("cine", "cinematic")):
        ITEMS.append((f"andor-osp_detail-{slug}_{word}", f"C3_{m}_{v}", f"Feature / material detail: {slug.replace('-', ' ')} ({word})", [960, 1920, 3000]))

for sub in ("00_index", "01_framer_upload", "02_responsive"):
    os.makedirs(os.path.join(KIT, sub), exist_ok=True)

rows = []
thumbs = []
for name, master, use, widths in ITEMS:
    src = os.path.join(masters, master + ".png")
    im = Image.open(src)
    alpha = im.mode in ("RGBA", "LA")
    im8 = im.convert("RGBA" if alpha else "RGB")      # 16-bit PNG -> 8-bit sRGB (what browsers show)
    up = os.path.join(KIT, "01_framer_upload", name + ".webp")
    im8.save(up, "WEBP", lossless=True, quality=100, method=6)
    resp = []
    for w in widths:
        for fmt in ("avif", "webp"):
            d = os.path.join(web, f"{master}-{w}.{fmt}")
            if os.path.exists(d):
                tgt = os.path.join(KIT, "02_responsive", f"{name}_{w}w.{fmt}")
                shutil.copyfile(d, tgt)
                resp.append(os.path.basename(tgt))
    rows.append({"asset": name, "master": master + ".png", "size": [im.width, im.height], "alpha": alpha, "use": use,
                 "framer_upload": os.path.basename(up), "framer_upload_bytes": os.path.getsize(up), "responsive": resp})
    t = im8.convert("RGBA")
    bg = Image.new("RGBA", t.size, (214, 210, 204, 255))
    bg.alpha_composite(t)
    t = bg.convert("RGB")
    t.thumbnail((420, 420))
    thumbs.append((t, name, use))

json.dump(rows, open(os.path.join(KIT, "00_index", "kit.json"), "w"), indent=1)

# Visual index: thumbnails with file name and use.
try:
    f1 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)
    f2 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
except OSError:
    f1 = f2 = ImageFont.load_default()
cols, cw, ch = 4, 440, 520
rowsn = (len(thumbs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * cw + 20, rowsn * ch + 80), (240, 237, 232))
d = ImageDraw.Draw(sheet)
d.text((20, 22), "ANDOR Website Asset Kit v1 · visual index (upload the 01_framer_upload file; Framer makes AVIF/WebP/srcset)", fill=(30, 28, 26), font=f1)
for i, (t, name, use) in enumerate(thumbs):
    x, y = 20 + (i % cols) * cw, 70 + (i // cols) * ch
    sheet.paste(t, (x, y))
    d.text((x, y + t.height + 8), name, fill=(30, 28, 26), font=f2)
    # wrap the use line
    words, line, yy = use.split(), "", y + t.height + 28
    for wd in words:
        if d.textlength(line + " " + wd, font=f2) > cw - 30:
            d.text((x, yy), line.strip(), fill=(90, 86, 80), font=f2)
            line, yy = wd, yy + 17
        else:
            line += " " + wd
    d.text((x, yy), line.strip(), fill=(90, 86, 80), font=f2)
sheet.save(os.path.join(KIT, "00_index", "visual_index.jpg"), quality=88)

L = ["# ANDOR Website Asset Kit v1", "",
     "Curated from the approved Gate C3 masters (Phase 4, M06 corrected). No typography is baked in: headlines, nav and CTAs stay live in Framer.", "",
     "## How to use in Framer", "",
     "- **Upload the `01_framer_upload` file.** It is lossless WebP: 8-bit sRGB, alpha kept on the transparent layers, at the master's full resolution.",
     "- **Framer optimises on upload.** It accepts JPEG, PNG, WebP and GIF, and serves AVIF, then WebP, with srcset by itself, so it never compresses an already-lossy file a second time.",
     "- **Hero sizing:** set heroes to *Fill* with *Cover*. Keep the product's side clear of live text: A1-R1 has the product right of centre. A1-R1 W moves it about 6 % further right, for the larger headline.",
     "- **Three-layer composite:** to place the product over a custom colour or gradient, stack `background-clean` (or your own fill), `shadow-only` and `product_transparent`, all at the same size and position. They come from one camera, so they register exactly. "
     "The shadow layer is near-opaque under the product's footprint, and the product covers that area only while the two stay registered: never move or scale the product layer on its own (for motion, use the rendered films, where the shadow is computed every frame).",
     "- **`02_responsive`:** the existing AVIF / WebP derivatives, copied unchanged, for code outside Framer. Use `<picture>` with AVIF first and WebP as the fallback.", "",
     "## Assets", "", "| Asset | Source master | Size | Alpha | Website use | Framer upload |", "|---|---|---|---|---|---|"]
for r in rows:
    L.append(f"| {r['asset']} | `{r['master']}` | {r['size'][0]} × {r['size'][1]} | {'yes' if r['alpha'] else 'no'} | {r['use']} | "
             f"`{r['framer_upload']}` ({r['framer_upload_bytes'] / 1e6:.1f} MB) |")
open(os.path.join(KIT, "00_index", "README.md"), "w").write("\n".join(L) + "\n")
print(len(rows), "assets")
