#!/usr/bin/env python3
"""Stage every image, film and font the Webflow page uses into one folder with uniform names, ready for the
R2 bucket:  img/<name>-<w>.{avif,webp}   film/<film>_<w>[_av1].mp4, film/<film>_{poster,still}.{webp,jpg}
font/<file>.woff2. Sources are the approved deliverables (the asset ZIPs' folders, the GUI captures and the
web motion set); nothing is re-rendered. Run with the scratch asset folders as arguments:

  stage_media.py <part1 dir> <web motion dir> <andor-web dir> <out dir>
"""
import glob, json, os, shutil, sys
from PIL import Image

P1, WM, WEB, OUT = sys.argv[1:5]
IMG, FILM, FONT = (os.path.join(OUT, d) for d in ("img", "film", "font"))
for d in (IMG, FILM, FONT):
    os.makedirs(d, exist_ok=True)
manifest = {}


def derive(src, name, widths, q_webp=84, q_avif=60, keep_alpha=False):
    """AVIF + WebP at each width (never upscaled)."""
    im = Image.open(src)
    im = im.convert("RGBA" if keep_alpha and im.mode in ("RGBA", "LA") else "RGB")
    done = []
    for w in widths:
        if w > im.width:
            continue
        r = im if w == im.width else im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
        r.save(os.path.join(IMG, f"{name}-{w}.webp"), "WEBP", quality=q_webp, method=6)
        r.save(os.path.join(IMG, f"{name}-{w}.avif"), "AVIF", quality=q_avif, speed=4)
        done.append(w)
    manifest[name] = {"widths": done, "ratio": round(im.height / im.width, 5), "source": os.path.relpath(src, os.path.dirname(P1))}


H = os.path.join(P1, "01_H01_front-facing-hero")
# hero plates (no type baked in): desktop 16:9 and a native phone portrait
derive(os.path.join(H, "C_website/andor-osp_H01-C_plate_desktop_3840x2160.jpg"), "hero-desktop", [1280, 1920, 2560, 3840])
derive(os.path.join(H, "C_website/andor-osp_H01-C_plate_phone_1290x2796.jpg"), "hero-phone", [640, 860, 1290])
# the dark theatre, with and without the wordmark (closing section)
derive(os.path.join(H, "A_dark-theatre/andor-osp_H01-A_wordmark_1920x1080.jpg"), "theatre-wordmark", [960, 1280, 1920])
derive(os.path.join(H, "A_dark-theatre/andor-osp_H01-A_clean_1920x1080.jpg"), "theatre-clean", [960, 1280, 1920])
derive(os.path.join(H, "B_ivory-gallery/andor-osp_H01-B_clean_1920x1080.jpg"), "gallery-ivory", [960, 1280, 1920])

# the shipping interface (0.9.0-beta.1, 32e0bee), three layers: the real editor, captured headlessly
derive(os.path.join(WEB, "assets/source/captures/osp-0.9.0-beta.1/cur-hero-3L@2x.png"), "editor-beta-3l", [1080, 1600, 2400, 2896])

# detail stills M01-M06 (cinematic and sharp), from the website kit's lossless uploads
KIT = os.path.join(P1, "07_website-asset-kit_v1/01_framer_upload")
for m, slug in (("m01", "macro-knobs"), ("m02", "source-controls"), ("m03", "waveform-display"),
                ("m04", "keyboard"), ("m05", "timber-corner"), ("m06", "touch-strips")):
    for v, word in (("cine", "cinematic"), ("sharp", "sharp")):
        derive(os.path.join(KIT, f"andor-osp_detail-{slug}_{word}.webp"), f"detail-{m}-{v}", [720, 1080, 1600, 2400])

# GUI captures already in the site's optimized set (name-<w>.{avif,webp}): copied as they are
OPT = os.path.join(WEB, "assets/optimized")
for name in ("osp-empty", "osp-hero-1l", "osp-hero-2l", "osp-hero-3l", "osp-reimagined-mosaic", "dd-reimagined-kaleidoscope",
             "dd-reimagined-tape-frame", "dd-reimagined-toybox", "dd-reimagined-mosaic", "dd-reimagined-mirage",
             "feat-notemap", "feat-capture", "feat-keydna", "syn-card-sample", "syn-card-analog", "syn-card-imprint",
             "syn-card-motion", "gran-card", "det-mod"):
    ws = sorted({int(f.rsplit("-", 1)[1].split(".")[0]) for f in os.listdir(OPT) if f.startswith(name + "-") and f.rsplit("-", 1)[0] == name})
    for w in ws:
        for ext in ("avif", "webp"):
            shutil.copyfile(os.path.join(OPT, f"{name}-{w}.{ext}"), os.path.join(IMG, f"{name}-{w}.{ext}"))
    im = Image.open(os.path.join(OPT, f"{name}-{ws[-1]}.webp"))
    manifest[name] = {"widths": ws, "ratio": round(im.height / im.width, 5), "source": "andor-web/assets/optimized"}

# films: the web delivery set (H.264 854/1280/1920, AV1 1280/1920, posters, stills)
for f in sorted(os.listdir(WM)):
    shutil.copyfile(os.path.join(WM, f), os.path.join(FILM, f))
films = sorted({f.split("_poster")[0] for f in os.listdir(WM) if "_poster." in f})
for fid in films:
    im = Image.open(os.path.join(WM, f"{fid}_poster.jpg"))
    manifest["film:" + fid] = {"ratio": round(im.height / im.width, 5)}

# fonts (SIL OFL): Inter Tight variable (latin) and EB Garamond 400 + italic (latin), from @fontsource
for pat, out in (("inter-tight-latin-wght-normal-*.woff2", "inter-tight-var.woff2"),
                 ("eb-garamond-latin-400-normal-*.woff2", "eb-garamond-400.woff2"),
                 ("eb-garamond-latin-400-italic-*.woff2", "eb-garamond-400-italic.woff2")):
    src = glob.glob(os.path.join(WEB, "dist/assets", pat))[0]
    shutil.copyfile(src, os.path.join(FONT, out))

json.dump(manifest, open(os.path.join(OUT, "media.json"), "w"), indent=1)
total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
print(f"{len(manifest)} media entries, {sum(len(fs) for _, _, fs in os.walk(OUT))} files, {total / 1e6:.1f} MB -> {OUT}")
