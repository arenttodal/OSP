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
bounds = json.load(open(a.bounds))["components"]
def comps(cls):
    return [c for c in bounds if c["class"] == cls]
kb = comps("osp::plugin::OspKeyboard")[0]
KB_BOTTOM = kb["y"] + kb["h"]              # 1014 in every capture before C2.5
# C2.5: the current editor (three layers: shorter cards, everything under them 64 px higher, its own
# compact bottom edge) is cut where the trimmed editor ended relative to its keys, 32 px under them,
# so the approved physical front return keeps its proportions to the keys.
if round(cap.height / S) < 1086 and round(cap.height / S) != round(KB_BOTTOM + 32):
    cap = cap.crop((0, 0, cap.width, round((KB_BOTTOM + 32) * S)))
EW, EH = 1448, round(cap.height / S)
# Checkpoint 2: the approved editor ends at 1046 px (keys, a 2 px reveal, the 20 px soft apron); the
# old one at 1086 px (a "Ready" footer under the keyboard). Both are read here, so the old fascia can
# still be rebuilt from the old capture for like-for-like comparisons.
TRIMMED = EH < 1086
APRON_TOP = KB_BOTTOM + 10.0     # 1024: design::layout::apronTop on the checkpoint-2 branch
TEAK_BOTTOM = KB_BOTTOM + 6 if TRIMMED else 1072
cap.save(os.path.join(a.out, "osp_face.png"))
lum = cap.convert("L")

# Teak: the interior of both drawn cheeks (no drawn edge shading), stacked into one long strip
# so a 400 mm rail does not repeat the grain.
left = cap.crop((int(9.5 * S), int(17 * S), int(23 * S), int(TEAK_BOTTOM * S)))
right = cap.crop((int(1425 * S), int(17 * S), int(1438.5 * S), int(TEAK_BOTTOM * S))).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
strip = Image.new("RGB", (left.width, left.height + right.height))
strip.paste(left, (0, 0))
strip.paste(right.resize(left.size if right.width != left.width else right.size), (0, left.height))
strip.save(os.path.join(a.out, "teak_strip.png"))

def rect(c, inset=0.0):
    return [c["x"] + inset, c["y"] + inset, c["x"] + c["w"] - inset, c["y"] + c["h"] - inset]

def colour_at(x, y, r=3):
    box = cap.crop((int((x - r) * S), int((y - r) * S), int((x + r) * S), int((y + r) * S))).resize((1, 1), Image.Resampling.BOX)
    return list(box.getpixel((0, 0)))

# Raised surfaces (measured edges match the components' bounds within 1 px).
cards = [{"rect": rect(c, 0.5), "radius": 9.0} for c in comps("osp::plugin::EngineCard")]
mod = comps("osp::plugin::ModulationPanel")[0]
cards.append({"rect": [35.5, mod["y"] - 5.5, 1411.5, mod["y"] + 208.5], "radius": 9.0})   # macro + envelope panel

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
    ("osp::plugin::RoutingKey", 9.0, 1.1),                       # C2.5: the header's routing key
    ("osp::plugin::KeyboardUtilityActions::Key", 6.0, 1.0),      # C2.5: SETTINGS / INFO
]:
    for c in comps(cls):
        r = rect(c, 0.5)
        # C2.5: with three layers the card's four keys sit on the display's glass: artwork there
        if any(d["x"] <= c["x"] and c["x"] + c["w"] <= d["x"] + d["w"] and d["y"] <= c["y"] and c["y"] + c["h"] <= d["y"] + d["h"]
               for d in comps("osp::plugin::SourceDisplay")):
            continue
        if cls == "osp::plugin::ModeSelector":
            r = [c["x"] + 1, c["y"] + 4, c["x"] + c["w"] - 1, c["y"] + c["h"] - 4]
        buttons.append({"kind": cls.split("::")[-1], "rect": r, "radius": radius, "lift_mm": lift,
                        "side_rgb": colour_at((r[0] + r[2]) / 2, r[3] - 3, 2)})

# Displays: the three SourceDisplays and the envelope screen (measured dark area).
displays = [rect(c, -0.5) for c in comps("osp::plugin::SourceDisplay")]
env = comps("osp::plugin::EnvelopePanel")[0]
displays.append([964.5, env["y"] + 23.5, 1394.5, env["y"] + 117.5])

# Keyboard: the key bed and the 36 black keys, found in the capture (dark runs at y 950).
black = []
inrun = False
for x in range(int(kb["x"]) + 1, int(kb["x"] + kb["w"])):
    v = lum.getpixel((int(x * S), int((kb["y"] + 37) * S)))
    if v < 80 and not inrun:
        inrun, x0 = True, x
    elif v >= 80 and inrun:
        inrun = False
        black.append([x0 - 0.5, kb["y"], x + 0.5, kb["y"] + 62.5])

# The front return's ceramic: the trimmed editor drew an apron there; the current one ends its panel
# above the housing (no apron drawn), so the return takes the panel's own colour.
_t, _f = colour_at(700, APRON_TOP + 4, 2), colour_at(700, APRON_TOP + 16, 2)
APRON_RGB = (_t, _f) if sum(_f) / 3 > 190 else (colour_at(700, KB_BOTTOM + 12, 2), colour_at(700, KB_BOTTOM + 12, 2))

knobs = []
for k in json.load(open(a.knobs))["knobs"]:
    if k["r"] < 10:   # the SYNTH chips' tiny icon dials stay artwork
        continue
    kind = "macro" if k["r"] > 40 else ("mini" if k["mini"] else "layer")
    knobs.append({"kind": kind, "c": [k["x"], k["y"]], "r": k["r"], "angle": k["angle"]})

geometry = {
    "editor_px": [EW, EH],
    "capture_scale": S,
    "panel": {"rect": [34.3, 19.0, 1412.3, APRON_TOP if TRIMMED else 1071.5], "radius": 7.0},
    "cards": cards,
    "buttons": buttons,
    "displays": displays,
    "keyboard": {"bed": [kb["x"], kb["y"], kb["x"] + kb["w"], kb["y"] + kb["h"]], "black": black},
    "knobs": knobs,
    "frame": {"cheek_x": [25.0, 1423.0], "back_y": 13.0, "front_y": float(EH) if TRIMMED else 1076.0},
    "colours": {"tray": colour_at(30, 540, 2), "panel": colour_at(700, mod["y"] - 18, 4), "card": colour_at(250, 140, 3),
                "white_key": colour_at(158, kb["y"] + 37, 2)},
    **({"apron": {"top": APRON_TOP, "front": float(EH), "x": [25.0, 1423.0],
                  "rgb_top": APRON_RGB[0], "rgb_front": APRON_RGB[1]}} if TRIMMED else {}),
    "notes": "Knobs from web-10-hero-3L-nocaps.json (editor geometry); surfaces from web-08 bounds; black keys detected in the capture.",
}
json.dump(geometry, open(os.path.join(a.out, "geometry.json"), "w"), indent=1)
print(f"{len(knobs)} knobs, {len(buttons)} buttons, {len(cards)} cards, {len(displays)} displays, {len(black)} black keys, face {cap.size}")
