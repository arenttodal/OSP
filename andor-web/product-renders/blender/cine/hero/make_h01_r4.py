#!/usr/bin/env python3
"""H01 Front-Facing Monument, R4 refinement: writes hero/specs_r4/ (cine_rig.py specs held still,
rendered with --still 0). The approved R3 specs in hero/specs/ are left as they were.

The R3 review approved the geometry (square-on, no yaw or roll, gently elevated, 90 mm) and asked for:
- more product presence: the instrument at about 75-80 % of the frame width, and higher in frame,
  still whole and with the keyboard level. Same lens, so the camera moves closer (1.75 m -> D);
  less lens shift raises the instrument without tilting the camera;
- more depth in the ceramic, the timber and the controls (see LIGHTS below);
- C as a website plate: the product larger, and no baked-in headline or button (they are native
  website content, laid over the plate in HTML).

Product frame: metres from the instrument's centre at rest; x along the keys (right), y toward the
back, z up. The instrument is 0.40 x 0.27 m, 27 mm tall.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "specs_r4")
os.makedirs(OUT, exist_ok=True)

GALLERY_LIGHTS = ["Key softbox", "Edge strip", "Overhead", "Fill", "Front kicker", "Low rake"]
WARM, NEUTRAL, COOL = [1.0, 0.95, 0.88], [1.0, 0.975, 0.95], [0.93, 0.96, 1.0]
IVORY_CYC = {"rgb": [0.60, 0.565, 0.52], "roughness": 0.9, "radius": 1.4, "wall": 2.2, "height": 5.0, "width": 14.0}
CHARCOAL_CYC = dict(IVORY_CYC, rgb=[0.045, 0.042, 0.040], roughness=0.85)
DISPLAY_FLAG = {"name": "Display flag", "mirror": True, "aim": [0.0, 0.065, 0.012], "size": [0.9, 0.6], "distance": 1.0}

EL, LENS = 32.0, 90
D, SHIFT = float(os.environ.get("H01_D", 1.385)), float(os.environ.get("H01_SHIFT", 0.048))
DC, SHIFTC = float(os.environ.get("H01_DC", 1.60)), float(os.environ.get("H01_SHIFTC", 0.085))


def front(el, d, target=(0.0, 0.0, 0.0)):
    """camera on the centre line, `el` degrees above horizontal, `d` metres from the target."""
    e = math.radians(el)
    return [0.0, round(target[1] - d * math.cos(e), 4), round(target[2] + d * math.sin(e), 4)]


def light(name, position, target, size, power, rgb=NEUTRAL, spread=180, link="product"):
    return {"name": name, "position": position, "target": target, "size": size, "power": power, "rgb": rgb,
            "spread": spread, "link": link, "glossy_only": False}


def stage(cyc, lights, flags=()):
    return {"hide_floor": True, "cyc": cyc, "remove_lights": GALLERY_LIGHTS, "add_lights": lights,
            "flags": [DISPLAY_FLAG] + list(flags)}


frames = []


def frame(fid, title, cam, st, rationale, **kw):
    s = {"id": fid, "family": "H01 Front-Facing Monument (R4)", "variant": title, "setup": "ANDOR_GALLERY", "base_shot": "A2",
         "aspect": [16, 9], "seconds": 1.0, "handles": 0.0, "camera": cam, "stage": st, "rationale": rationale}
    s.update(kw)
    frames.append(s)


# A: dark theatre. Depth, compared with R3:
# - the top key moves further back and narrows, so the light falls off from the back edge toward
#   the keys: the surface reads as a plane receding, not a flat field;
# - the side rakes come lower and slightly behind, so each knob throws a longer shadow forward and
#   its knurled skirt catches a rim;
# - two low, narrow kickers skim the teak cheeks from the front corners, drawing the grain and the
#   timber's edge against the charcoal;
# - the flat front lift is halved (it filled the shadows that give the controls their depth).
A_LIGHTS = [light("Top key", [0.0, 0.85, 0.78], [0.0, 0.04, 0.0], [1.0, 0.32], 24, WARM),
            light("Rake left", [-0.90, 0.26, 0.075], [0.0, 0.0, 0.0], [0.5, 0.08], 13, WARM),
            light("Rake right", [0.90, 0.26, 0.075], [0.0, 0.0, 0.0], [0.5, 0.08], 13, WARM),
            light("Cheek kicker left", [-0.62, -0.42, 0.05], [-0.20, 0.0, 0.0], [0.10, 0.25], 1.6, WARM),
            light("Cheek kicker right", [0.62, -0.42, 0.05], [0.20, 0.0, 0.0], [0.10, 0.25], 1.6, WARM),
            light("Front lift", [0.0, -1.2, 0.55], [0.0, 0.0, 0.0], [1.6, 0.6], 0.4, NEUTRAL),
            light("Floor pool", [0.0, 0.60, 1.30], [0.0, 0.60, 0.0], [0.35, 0.35], 16, WARM, spread=40, link=None)]

# B: ivory gallery. Depth, compared with R3: the same high-behind key (moved back, so the surface
# falls off toward the keys), the left rake lower for longer knob shadows, the right rake now a
# narrow strip behind-right that rims the knobs and the right cheek, and the two front flags
# brought closer so the near cheeks and the front edge hold their shade (timber against ivory).
B_LIGHTS = [light("Top key", [0.0, 0.90, 0.80], [0.0, 0.04, 0.0], [1.1, 0.36], 24, WARM),
            light("Rake left", [-0.95, 0.28, 0.075], [0.0, 0.0, 0.0], [0.6, 0.12], 34, WARM),
            light("Rim right", [0.80, 0.45, 0.12], [0.0, 0.0, 0.0], [0.08, 0.40], 9, NEUTRAL),
            light("Cove light", [0.0, 0.90, 1.40], [0.0, 0.85, 0.0], [0.9, 0.9], 40, WARM, spread=55, link=None)]
B_FLAGS = [{"name": "Flag left", "mode": "negative", "position": [-0.45, -0.38, 0.08], "target": [0, 0, 0], "size": [0.9, 0.6]},
           {"name": "Flag right", "mode": "negative", "position": [0.45, -0.38, 0.08], "target": [0, 0, 0], "size": [0.9, 0.6]}]

frame("H01_A_R4", "Dark Theatre (R4)",
      {"lens": LENS, "fstop": 11, "position": front(EL, D), "target": [0.0, 0.0, 0.0], "shift": [0.0, SHIFT]},
      stage(CHARCOAL_CYC, A_LIGHTS),
      world=0.04, emission=0.9, exposure=0.0, typography={"layout": "H01_A_R4"},
      rationale="R3's square-on dark theatre, with the instrument at about 78 % of the frame width and lifted toward the centre. The light now falls off from the back edge toward the keys, the knobs throw longer shadows from low side rakes, and two narrow kickers draw the teak cheeks against the charcoal.")

frame("H01_B_R4", "Ivory Gallery (R4)",
      {"lens": LENS, "fstop": 11, "position": front(EL, D), "target": [0.0, 0.0, 0.0], "shift": [0.0, SHIFT]},
      stage(IVORY_CYC, B_LIGHTS, B_FLAGS),
      world=0.05, exposure=-0.2, typography={"layout": "H01_B_R4"},
      rationale="R3's ivory portrait, larger and higher in frame. The surface falls off from the back edge, a low rake from the left models the knobs, a narrow rim from behind-right draws the right side, and close flags keep the timber cheeks in shade against the ivory.")

# C: the website plate. B's light, the instrument larger than in R3 (about 66 % of the width) and
# low enough to leave the top band free for the headline and button, which the website draws.
frame("H01_C_R4", "Desktop website plate (R4)",
      {"lens": LENS, "fstop": 11, "position": front(EL, DC), "target": [0.0, 0.0, 0.0], "shift": [0.0, SHIFTC]},
      stage(IVORY_CYC, B_LIGHTS, B_FLAGS),
      world=0.05, exposure=-0.2, typography={"layout": "H01_C_R4"},
      rationale="The website hero plate: B's ivory light, the instrument larger than in R3 and set in the lower two thirds, the top band left as clean ivory for the website's own headline and call to action. No type is baked into the plate.")

for s in frames:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(frames), "hero specs ->", OUT, "D", D, "shift", SHIFT, "DC", DC, "shiftC", SHIFTC)
