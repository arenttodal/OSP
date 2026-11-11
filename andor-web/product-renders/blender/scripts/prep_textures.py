"""Prepare the textures the Blender scene uses, all derived from the genuine OSP capture.

Run with any Python 3 + Pillow, from andor-web/:
  python3 product-renders/blender/scripts/prep_textures.py [--capture assets/source/captures/web-01-hero-3L@3x.png]

Writes product-renders/blender/textures/:
  osp_face.png        the capture itself (3x, 4344 x 3258), the only source of the interface
  osp_display_mask.png  white where the editor draws a dark display (screens get glass + glow)
  teak_strip.png      the interior of the editor's own teak cheek (grain only, no drawn shading)
  geometry.json       measured editor geometry (1x px) the scene builds from
"""
import argparse
import json
import os

from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument("--capture", default="assets/source/captures/web-01-hero-3L@3x.png")
p.add_argument("--bounds", default="assets/source/captures/web-08-hero-3L-bounds.json")
p.add_argument("--out", default="product-renders/blender/textures")
a = p.parse_args()
os.makedirs(a.out, exist_ok=True)

cap = Image.open(a.capture).convert("RGB")
S = cap.width / 1448  # capture scale (3 for @3x)
cap.save(os.path.join(a.out, "osp_face.png"))

bounds = json.load(open(a.bounds))["components"]
def comps(cls):
    return [c for c in bounds if c["class"] == cls]

# Displays: the three SourceDisplays (their bounds) and the envelope screen (measured: the dark
# area inside EnvelopePanel). Corner radius measured at ~8 px.
displays = [(c["x"] - 1, c["y"] - 1, c["x"] + c["w"] + 1, c["y"] + c["h"] + 1) for c in comps("osp::plugin::SourceDisplay")]
displays.append((965, 712, 1394, 805))
mask = Image.new("L", cap.size, 0)
d = ImageDraw.Draw(mask)
for (x0, y0, x1, y1) in displays:
    d.rounded_rectangle((x0 * S, y0 * S, x1 * S, y1 * S), radius=8 * S, fill=255)
mask.save(os.path.join(a.out, "osp_display_mask.png"))

# Teak: the interior of the left cheek (x 12-72 @3x avoids its drawn rounded-edge shading).
teak = cap.crop((int(4 * S), int(17 * S), int(24 * S), int(1072 * S)))
teak.save(os.path.join(a.out, "teak_strip.png"))

def centre(c):
    return (c["x"] + c["w"] / 2, c["y"] + c["h"] / 2)

knobs = []
for c in comps("juce::Slider"):
    if c["w"] == 132:  # the six macro knobs
        knobs.append({"kind": "macro", "c": centre(c), "r": 47.0})
    elif c["w"] == 48:  # envelope A/D/S/R
        knobs.append({"kind": "mini", "c": centre(c), "r": 19.0})
for c in comps("osp::plugin::LayerKnob::Dial"):
    knobs.append({"kind": "layer", "c": centre(c), "r": 28.0})

geometry = {
    "editor_px": [1448, 1086],
    "capture_scale": S,
    "panel": {"rect": [34.3, 19.0, 1412.3, 1071.5], "radius": 7.0},
    "cheeks": [{"rect": [3.0, 13.0, 25.0, 1076.0]}, {"rect": [1423.0, 13.0, 1445.0, 1076.0]}],
    "displays": displays,
    "knobs": knobs,
    "notes": "Measured from web-01-hero-3L@3x.png (luminance profiles) and web-08-hero-3L-bounds.json.",
}
json.dump(geometry, open(os.path.join(a.out, "geometry.json"), "w"), indent=1)
print(f"{len(knobs)} knobs, {len(displays)} displays, face {cap.size}")
