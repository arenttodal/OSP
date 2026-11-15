"""Prepare textures and the layered geometry the Blender scene builds from — all from the genuine
OSP editor.

Sources (assets/source/captures/, regenerate with tools/capture.sh; OSP test [web-capture]):
  web-10-hero-3L-nocaps@4x.png   the three-layer hero with every knob's ticks and arcs but no cap
                                 (a test-only switch in design::draw::knob), so 3D caps can stand on
                                 a clean panel. Identical to web-01-hero-3L everywhere else.
  web-10-hero-3L-nocaps.json     each knob's centre, cap radius and pointer angle, as the editor
                                 computes them (editor px, angle clockwise from 12 o'clock).
  web-08-hero-3L-bounds.json     every component's bounds (cards, buttons, displays, keyboard).

Run with Python 3 + Pillow, from andor-web/:
  python3 product-renders/blender/scripts/prep_textures.py

Writes product-renders/blender/textures/:
  osp_face.png      the clean capture (4x)            teak_strip.png  the editor's own teak, both cheeks
  geometry.json     every layer's measured geometry (1x editor px)
"""
import argparse
import json
import os

from PIL import Image

p = argparse.ArgumentParser()
p.add_argument("--capture", default="assets/source/captures/web-10-hero-3L-nocaps@4x.png")
p.add_argument("--knobs", default="assets/source/captures/web-10-hero-3L-nocaps.json")
p.add_argument("--bounds", default="assets/source/captures/web-08-hero-3L-bounds.json")
p.add_argument("--out", default="product-renders/blender/textures")
a = p.parse_args()
os.makedirs(a.out, exist_ok=True)

cap = Image.open(a.capture).convert("RGB")
S = cap.width / 1448
cap.save(os.path.join(a.out, "osp_face.png"))
lum = Image.open(a.capture).convert("L")

# Teak: the interior of both drawn cheeks (no drawn edge shading), stacked into one long strip
# so a 400 mm rail does not repeat the grain.
left = cap.crop((int(9.5 * S), int(17 * S), int(23 * S), int(1072 * S)))
right = cap.crop((int(1425 * S), int(17 * S), int(1438.5 * S), int(1072 * S))).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
strip = Image.new("RGB", (left.width, left.height + right.height))
strip.paste(left, (0, 0))
strip.paste(right.resize(left.size if right.width != left.width else right.size), (0, left.height))
strip.save(os.path.join(a.out, "teak_strip.png"))

bounds = json.load(open(a.bounds))["components"]
def comps(cls):
    return [c for c in bounds if c["class"] == cls]
def rect(c, inset=0.0):
    return [c["x"] + inset, c["y"] + inset, c["x"] + c["w"] - inset, c["y"] + c["h"] - inset]

def colour_at(x, y, r=3):
    box = cap.crop((int((x - r) * S), int((y - r) * S), int((x + r) * S), int((y + r) * S))).resize((1, 1), Image.Resampling.BOX)
    return list(box.getpixel((0, 0)))

# Raised surfaces (measured edges match the components' bounds within 1 px).
cards = [{"rect": rect(c, 0.5), "radius": 9.0} for c in comps("osp::plugin::EngineCard")]
cards.append({"rect": [35.5, 682.5, 1411.5, 896.5], "radius": 9.0})   # macro + envelope panel

buttons = []
for cls, radius, lift in [
    ("osp::plugin::ModifierButton", 6.0, 1.3),
    ("osp::plugin::EqButton", 5.0, 1.0),
    ("osp::plugin::RecButton", 5.0, 1.0),
    ("osp::plugin::ModeSelector", 5.0, 1.0),
    ("osp::plugin::LayerStateButton", 4.0, 0.8),
    ("osp::plugin::PresetBar", 9.0, 1.1),
    ("osp::plugin::SavePresetButton", 9.0, 1.1),
    ("osp::plugin::ArpControl", 7.0, 1.1),
    ("osp::plugin::AdvancedCardButton", 7.0, 1.1),
]:
    for c in comps(cls):
        r = rect(c, 0.5)
        if cls == "osp::plugin::ModeSelector":
            r = [c["x"] + 1, c["y"] + 4, c["x"] + c["w"] - 1, c["y"] + c["h"] - 4]
        buttons.append({"kind": cls.split("::")[-1], "rect": r, "radius": radius, "lift_mm": lift,
                        "side_rgb": colour_at((r[0] + r[2]) / 2, r[3] - 3, 2)})

# Displays: the three SourceDisplays and the envelope screen (measured dark area).
displays = [rect(c, -0.5) for c in comps("osp::plugin::SourceDisplay")]
displays.append([964.5, 711.5, 1394.5, 805.5])

# Keyboard: the key bed and the 36 black keys, found in the capture (dark runs at y 950).
kb = comps("osp::plugin::OspKeyboard")[0]
black = []
inrun = False
for x in range(int(kb["x"]) + 1, int(kb["x"] + kb["w"])):
    v = lum.getpixel((int(x * S), int(950 * S)))
    if v < 80 and not inrun:
        inrun, x0 = True, x
    elif v >= 80 and inrun:
        inrun = False
        black.append([x0 - 0.5, 913.0, x + 0.5, 975.5])

knobs = []
for k in json.load(open(a.knobs))["knobs"]:
    if k["r"] < 10:   # the SYNTH chips' tiny icon dials stay artwork
        continue
    kind = "macro" if k["r"] > 40 else ("mini" if k["mini"] else "layer")
    knobs.append({"kind": kind, "c": [k["x"], k["y"]], "r": k["r"], "angle": k["angle"]})

geometry = {
    "editor_px": [1448, 1086],
    "capture_scale": S,
    "panel": {"rect": [34.3, 19.0, 1412.3, 1071.5], "radius": 7.0},
    "cards": cards,
    "buttons": buttons,
    "displays": displays,
    "keyboard": {"bed": [kb["x"], kb["y"], kb["x"] + kb["w"], kb["y"] + kb["h"]], "black": black},
    "knobs": knobs,
    "frame": {"cheek_x": [25.0, 1423.0], "back_y": 13.0, "front_y": 1076.0},
    "colours": {"tray": colour_at(30, 540, 2), "panel": colour_at(700, 670, 4), "card": colour_at(250, 140, 3)},
    "notes": "Knobs from web-10-hero-3L-nocaps.json (editor geometry); surfaces from web-08 bounds; black keys detected in the capture.",
}
json.dump(geometry, open(os.path.join(a.out, "geometry.json"), "w"), indent=1)
print(f"{len(knobs)} knobs, {len(buttons)} buttons, {len(cards)} cards, {len(displays)} displays, {len(black)} black keys, face {cap.size}")
