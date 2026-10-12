"""From master renders to comparison sheets and runtime resources (Python 3 + Pillow; no
Blender needed).

    python3 postprocess.py --config config/macro-knob.json --stage variants
    python3 postprocess.py --config config/macro-knob.json --stage indicators
    python3 postprocess.py --config config/macro-knob.json --stage cameras
    python3 postprocess.py --config config/macro-knob.json --stage frames

Sheets show every candidate at the sizes the plugin really draws it (the macro body at the
default window size, 1x and Retina), on the panel's own colour, next to the current knob.

The frames stage writes, per runtime level (frame side in pixels):
    <runtime destination>/<name>-<size>.zip   manifest.json + frames/000.png ... (stored, not
                                              deflated: PNGs are compressed already); the
                                              plugin decodes a frame only when it is shown
and, for people and other tools, under output/<name>/:
    atlas-<size>.png      every frame in a grid (row-major, frame 0 top left)
    manifest.json         the same manifest, plus the atlas layout
    previews/contact-sheet.png
"""

import argparse
import io
import json
import math
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from osp_assets import config as cfg  # noqa: E402

REPO = os.path.dirname(os.path.dirname(cfg.ROOT))
FONT = os.path.join(REPO, "apps", "plugin", "Resources", "fonts", "Inter-Regular.ttf")
FONT_MEDIUM = os.path.join(REPO, "apps", "plugin", "Resources", "fonts", "Inter-Medium.ttf")

# The plugin's macro panel behind the knobs (sampled from the instrument at 1448 x 1086) and
# the macro knob's body diameter there (design::draw::knob: 132 px box, ticks: / 1.42).
PANEL = (231, 224, 211)
TEXT = (52, 48, 44)
DESIGN_BODY = 2.0 * 0.5 * 132.0 / 1.42
# Window scales: the default (1086 px wide) and the largest (1810 px) of the 1448 design.
SIZES = [("1x, default window", DESIGN_BODY * 1086 / 1448),
         ("Retina, default window", 2.0 * DESIGN_BODY * 1086 / 1448),
         ("Retina, largest window", 2.0 * DESIGN_BODY * 1810 / 1448)]
MANIFEST_SCHEMA = 1


def font(size, medium=False):
    try:
        return ImageFont.truetype(FONT_MEDIUM if medium else FONT, size)
    except OSError:
        return ImageFont.load_default()


def resize(image, side):
    """Lanczos in premultiplied alpha (no dark or light fringes at the edge)."""
    side = max(1, int(round(side)))
    if image.size == (side, side):
        return image.copy()
    return image.convert("RGBa").resize((side, side), Image.LANCZOS).convert("RGBA")


_FADE_MASKS = {}


def edge_fade(image, inner=0.82, outer=0.985):
    """The soft shadow fades to nothing before the frame's edge (never a cut-off line)."""
    from PIL import ImageChops
    w, h = image.size
    key = (w, h, inner, outer)
    if key not in _FADE_MASKS:
        mask = Image.new("L", (w, h))
        px = mask.load()
        cx, cy, half = (w - 1) / 2.0, (h - 1) / 2.0, w / 2.0
        for y in range(h):
            for x in range(w):
                t = min(1.0, max(0.0, (math.hypot(x - cx, y - cy) / half - inner) / (outer - inner)))
                px[x, y] = int(round(255.0 * (1.0 - t * t * (3.0 - 2.0 * t))))
        _FADE_MASKS[key] = mask
    out = image.copy()
    out.putalpha(ImageChops.multiply(image.getchannel("A"), _FADE_MASKS[key]))
    return out


def on_panel(image, colour=PANEL):
    bg = Image.new("RGBA", image.size, colour + (255,))
    bg.alpha_composite(image)
    return bg.convert("RGB")


def frame_side_for_body(body_px, margin):
    return body_px * margin


def margin_of(config):
    return cfg.camera(config["camera"])["margin"]


OUT_OVERRIDE = None


def out_dir(config):
    return OUT_OVERRIDE or os.path.join(cfg.ROOT, "output", config["name"])


def current_knob_crop():
    """The current vector knob (LIFE at 50 %) from a canonical screenshot, if one is around."""
    path = os.environ.get("OSP_CURRENT_KNOB_SHOT")
    if not path or not os.path.exists(path):
        return None
    shot = Image.open(path).convert("RGB")
    # LIFE's knob centre in the 1448 x 1086 instrument: (128, 818); box 132 px.
    return shot.crop((128 - 66, 818 - 66, 128 + 66, 818 + 66))


def sheet(columns, title, path, margin, note=None):
    """columns: [(label, RGBA master)] -> one image: a large view and each real size."""
    big = 300
    pad, head = 28, 70
    sizes = [(label, frame_side_for_body(body, margin)) for label, body in SIZES]
    col_w = max(big, 220) + pad
    row_h = [big] + [int(math.ceil(s)) for _, s in sizes]
    width = pad + col_w * len(columns) + 150
    height = head + sum(h + 46 for h in row_h) + (40 if note else 10)
    img = Image.new("RGB", (width, height), PANEL)
    d = ImageDraw.Draw(img)
    d.text((pad, 18), title, fill=TEXT, font=font(22, True))
    y = head
    labels = [("Large (inspection)", big)] + [(f"{label}: body {body:.0f} px", s) for (label, body), (_, s) in zip(SIZES, sizes)]
    for (row_label, side), h in zip(labels, row_h):
        d.text((pad, y), row_label, fill=TEXT, font=font(14))
        y += 22
        for i, (label, master) in enumerate(columns):
            x = pad + 150 + i * col_w
            if master is None:
                continue
            if isinstance(master, Image.Image) and master.mode == "RGB":
                # A screenshot crop (already on the panel): scaled like the renders.
                body_in_crop = DESIGN_BODY / 132.0
                target = int(round(side / margin / body_in_crop))
                tile = master.resize((target, target), Image.LANCZOS)
            else:
                tile = on_panel(resize(master, side))
            img.paste(tile, (x + (col_w - pad - tile.size[0]) // 2, y))
        y += h + 24
    for i, (label, _) in enumerate(columns):
        x = pad + 150 + i * col_w
        d.text((x, head - 26), label, fill=TEXT, font=font(15, True))
    if note:
        d.text((pad, height - 30), note, fill=TEXT, font=font(13))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print(f"wrote {os.path.relpath(path)}")


def stage_stills(config, folder, names, labels, title, filename, note=None):
    base = os.path.join(out_dir(config), folder)
    columns = []
    current = current_knob_crop()
    if current is not None:
        columns.append(("Current (vector)", current))
    for name, label in zip(names, labels):
        path = os.path.join(base, f"{name}.png")
        columns.append((label, edge_fade(Image.open(path).convert("RGBA")) if os.path.exists(path) else None))
    sheet(columns, title, os.path.join(out_dir(config), "previews", filename), margin_of(config), note)


def atlas_layout(count):
    columns = int(math.ceil(math.sqrt(count)))
    return columns, int(math.ceil(count / columns))


def manifest_for(config, info, side, count):
    rot = config["rotation"]
    margin = margin_of(config)
    return {
        "schemaVersion": MANIFEST_SCHEMA,
        "name": config["name"],
        "type": config["type"],
        "frameCount": count,
        "frameSize": side,
        "pivot": [0.5, 0.5],
        "bodyRadius": round(0.5 / margin, 6),
        "angleStart": rot["start"],
        "angleEnd": rot["end"],
        "angleConvention": "radians clockwise from 12 o'clock (juce::Slider rotary); frame i shows angleStart + i / (frameCount - 1) * (angleEnd - angleStart)",
        "frames": "frames/%03d.png",
        "pixelFormat": "PNG RGBA 8-bit, straight alpha, sRGB",
        "source": {
            "generator": "tools/blender-assets",
            "configHash": info.get("configHash"),
            "blender": info.get("blender"),
            "material": info.get("material"),
            "exposure": info.get("exposure"),
            "masterResolution": info.get("resolution"),
            "samples": info.get("samples"),
        },
    }


def stage_frames(config):
    base = out_dir(config)
    with open(os.path.join(base, "render-info.json"), "r", encoding="utf-8") as f:
        info = json.load(f)
    count = info["frameCount"]
    masters = []
    for i in range(count):
        path = os.path.join(base, "master", f"frame_{i:03d}.png")
        if not os.path.exists(path):
            raise SystemExit(f"missing {path}: render the frames first")
        masters.append(edge_fade(Image.open(path).convert("RGBA")))
    sizes = {m.size for m in masters}
    if len(sizes) != 1:
        raise SystemExit(f"frames differ in size: {sizes}")

    dest = os.path.join(REPO, config["runtime"]["destination"])
    os.makedirs(dest, exist_ok=True)
    manifests = {}
    for side in config["runtime"]["levels"]:
        frames = [resize(m, side) for m in masters]
        manifest = manifest_for(config, info, side, count)
        manifests[side] = manifest
        # The runtime pack: a zip of PNGs, stored (each PNG is compressed already), fixed
        # timestamps and order so the same frames always give the same bytes.
        pack = os.path.join(dest, f"{config['name']}-{side}.zip")
        with zipfile.ZipFile(pack, "w", compression=zipfile.ZIP_STORED) as z:
            def add(name, data):
                entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                entry.external_attr = 0o644 << 16
                z.writestr(entry, data)
            add("manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
            for i, frame in enumerate(frames):
                buf = io.BytesIO()
                frame.save(buf, format="PNG", optimize=True)
                add(f"frames/{i:03d}.png", buf.getvalue())
        print(f"wrote {os.path.relpath(pack, REPO)} ({os.path.getsize(pack) / 1024:.0f} KB)")
        # The atlas, for people and other tools.
        cols, rows = atlas_layout(count)
        atlas = Image.new("RGBA", (cols * side, rows * side), (0, 0, 0, 0))
        for i, frame in enumerate(frames):
            atlas.paste(frame, ((i % cols) * side, (i // cols) * side))
        atlas.save(os.path.join(base, f"atlas-{side}.png"), optimize=True)

    layout = {str(side): {"file": f"atlas-{side}.png", "columns": atlas_layout(count)[0], "rows": atlas_layout(count)[1],
                          "order": "row-major from the top left"} for side in manifests}
    top = dict(manifests[max(manifests)])
    top["levels"] = sorted(manifests)
    top["atlases"] = layout
    with open(os.path.join(base, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(top, f, indent=2, sort_keys=True)
        f.write("\n")

    # Contact sheet: every 8th frame and the last, at the Retina default size, numbered.
    margin = margin_of(config)
    side = int(round(frame_side_for_body(SIZES[1][1], margin)))
    picks = list(range(0, count, 8))
    if picks[-1] != count - 1:
        picks.append(count - 1)
    cols = 6
    cell = side + 16
    rows = int(math.ceil(len(picks) / cols))
    sheet_img = Image.new("RGB", (cols * cell + 32, rows * (cell + 22) + 64), PANEL)
    d = ImageDraw.Draw(sheet_img)
    d.text((16, 14), f"{config['name']}: {count} frames, {math.degrees(config['rotation']['end'] - config['rotation']['start']):.0f} degrees, every 8th at the Retina default size",
           fill=TEXT, font=font(16, True))
    for k, i in enumerate(picks):
        x, y = 16 + (k % cols) * cell, 50 + (k // cols) * (cell + 22)
        sheet_img.paste(on_panel(resize(masters[i], side)), (x, y))
        d.text((x, y + side + 2), f"frame {i}  ({100.0 * i / (count - 1):.0f} %)", fill=TEXT, font=font(12))
    path = os.path.join(base, "previews", "contact-sheet.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet_img.save(path, optimize=True)
    print(f"wrote {os.path.relpath(path)}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", required=True)
    p.add_argument("--stage", required=True, choices=["variants", "indicators", "cameras", "frames"])
    p.add_argument("--set", action="append", default=[], metavar="KEY=VALUE")
    p.add_argument("--out", default=None, help="the render folder (default output/<name>), as given to generate.py")
    args = p.parse_args()
    global OUT_OVERRIDE
    OUT_OVERRIDE = args.out
    config = cfg.load(args.config, args.set)
    if args.stage == "variants":
        names = config["variants"]
        stage_stills(config, "variants", names, [cfg.material(n)["label"] for n in names],
                     f"{config['name']}: material variants (same geometry, camera, light and exposure)", "variants.png")
    elif args.stage == "indicators":
        stage_stills(config, "indicators", ["engraved", "printed"], ["Engraved + inlay", "Printed"],
                     f"{config['name']}: indicator, engraved inlay vs printed (at 62 %)", "indicators.png")
    elif args.stage == "cameras":
        with open(os.path.join(cfg.ROOT, "scenes", "cameras.json"), "r", encoding="utf-8") as f:
            names = sorted(json.load(f)["presets"])
        stage_stills(config, "cameras", names, names, f"{config['name']}: camera elevation", "cameras.png")
    else:
        stage_frames(config)


if __name__ == "__main__":
    main()
