#!/usr/bin/env python3
"""H01 Front-Facing Monument: writes the hero still specs into hero/specs/ (cine_rig.py specs held
still, rendered with --still 0).

The brief, as geometry:
- the instrument faces the viewer: product turn 0, tilt 0, no roll; the camera sits on the
  instrument's centre line (x = 0) and looks at its centre, so both teak cheeks are mirror images;
- gently elevated: high enough to read the control surface, the three displays, the macros and the
  keys, low enough that the front edge keeps its thickness (no thin slab, no plan view);
- the instrument is placed low in frame with lens shift, not by tilting the camera down, so the
  verticals stay vertical and the perspective is physical;
- 85-100 mm: a long lens keeps the near and far edges close in width (a calm, substantial shape).

Product frame: metres from the instrument's centre at rest; x along the keys (right), y toward the
back, z up. The instrument is 0.40 x 0.27 m, 27 mm tall.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "specs")
os.makedirs(OUT, exist_ok=True)

GALLERY_LIGHTS = ["Key softbox", "Edge strip", "Overhead", "Fill", "Front kicker", "Low rake"]
WARM, NEUTRAL, COOL = [1.0, 0.95, 0.88], [1.0, 0.975, 0.95], [0.93, 0.96, 1.0]
IVORY_CYC = {"rgb": [0.60, 0.565, 0.52], "roughness": 0.9, "radius": 1.4, "wall": 2.2, "height": 5.0, "width": 14.0}
CHARCOAL_CYC = dict(IVORY_CYC, rgb=[0.045, 0.042, 0.040], roughness=0.85)
DISPLAY_FLAG = {"name": "Display flag", "mirror": True, "aim": [0.0, 0.065, 0.012], "size": [0.9, 0.6], "distance": 1.0}


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
    s = {"id": fid, "family": "H01 Front-Facing Monument", "variant": title, "setup": "ANDOR_GALLERY", "base_shot": "A2",
         "aspect": [16, 9], "seconds": 1.0, "handles": 0.0, "camera": cam, "stage": st, "rationale": rationale}
    s.update(kw)
    frames.append(s)


EL, LENS = 32.0, 90
# A: dark theatre. A broad soft top light from just behind the instrument lights the control surface
# and draws the back edge; two low rakes from the sides model the knobs and the teak cheeks equally;
# a separate glow on the charcoal wall gives the wordmark a field to sit in. Everything that lights
# the instrument is linked to it, so the backdrop stays charcoal and the floor holds no hotspot.
frame("H01_A", "Dark Theatre",
      {"lens": LENS, "fstop": 11, "position": front(EL, 1.75), "target": [0.0, 0.0, 0.0], "shift": [0.0, 0.11]},
      stage(CHARCOAL_CYC,
            [light("Top key", [0.0, 0.40, 0.95], [0.0, 0.0, 0.0], [1.3, 0.55], 26, WARM),
             light("Rake left", [-0.95, 0.10, 0.16], [0.0, 0.0, 0.0], [0.5, 0.12], 6, WARM),
             light("Rake right", [0.95, 0.10, 0.16], [0.0, 0.0, 0.0], [0.5, 0.12], 6, WARM),
             light("Front lift", [0.0, -1.2, 0.55], [0.0, 0.0, 0.0], [1.6, 0.6], 1.5, NEUTRAL),
             # from the raised front, the backdrop above the instrument is the floor behind it: the
             # wordmark's field is a soft warm pool there, falling off to charcoal at the edges
             light("Floor pool", [0.0, 0.60, 1.30], [0.0, 0.60, 0.0], [0.35, 0.35], 16, WARM, spread=40, link=None)]),
      world=0.04, emission=0.9, exposure=0.0, typography={"layout": "H01_A"},
      rationale="The instrument faces us, centred and grounded on a charcoal sweep, seen from a gently raised front so the whole surface reads: three displays, six macros, the keys and both teak cheeks. A soft top light from just behind draws the back edge and lays a warm sheen across the ceramic; two low rakes from left and right model every knob symmetrically. A pool of warm light on the wall behind gives the monumental wordmark its field; the instrument's back edge crosses the feet of the letters.")

# B: ivory gallery. The B1 relight's language made symmetric: a soft key from high behind for the
# sheen, a low warm rake from the left for modelling and a weaker one from the right, black flags at
# the front sides so the near cheeks hold shape, and the cove falling from warm light to shade.
frame("H01_B", "Ivory Gallery",
      {"lens": LENS, "fstop": 11, "position": front(EL, 1.75), "target": [0.0, 0.0, 0.0], "shift": [0.0, 0.11]},
      stage(IVORY_CYC,
            [light("Top key", [0.0, 0.55, 0.95], [0.0, 0.0, 0.0], [1.4, 0.6], 26, WARM),
             light("Rake left", [-0.95, 0.20, 0.15], [0.0, 0.0, 0.0], [0.6, 0.20], 20, WARM),
             light("Rake right", [0.95, 0.20, 0.15], [0.0, 0.0, 0.0], [0.6, 0.20], 5, NEUTRAL),
             # the cove: a broad warm pool behind the instrument, so the ivory falls from light
             # behind the product to shade at the frame's edges (no flat catalogue field)
             light("Cove light", [0.0, 0.90, 1.40], [0.0, 0.85, 0.0], [0.9, 0.9], 40, WARM, spread=55, link=None)],
            [{"name": "Flag left", "mode": "negative", "position": [-0.55, -0.45, 0.10], "target": [0, 0, 0], "size": [0.9, 0.6]},
             {"name": "Flag right", "mode": "negative", "position": [0.55, -0.45, 0.10], "target": [0, 0, 0], "size": [0.9, 0.6]}]),
      world=0.05, exposure=-0.2, typography={"layout": "H01_B"},
      rationale="The ivory portrait, square-on: the instrument centred on a seamless warm cyclorama with no horizon line, a soft light from high behind giving the ceramic its sheen and the back edge a bright line, a low warm rake from the left modelling the knobs. The wordmark sits behind in a darker warm grey, printed into the cove's own light, so the gradient passes through the letters.")

# C: desktop website. The ivory light of B, the instrument smaller and lower so the top third holds
# the headline and the call to action above the wordmark.
frame("H01_C", "Desktop website",
      {"lens": LENS, "fstop": 11, "position": front(EL, 2.35), "target": [0.0, 0.0, 0.0], "shift": [0.0, 0.165]},
      stage(IVORY_CYC,
            [light("Top key", [0.0, 0.55, 0.95], [0.0, 0.0, 0.0], [1.4, 0.6], 26, WARM),
             light("Rake left", [-0.95, 0.20, 0.15], [0.0, 0.0, 0.0], [0.6, 0.20], 20, WARM),
             light("Rake right", [0.95, 0.20, 0.15], [0.0, 0.0, 0.0], [0.6, 0.20], 5, NEUTRAL),
             # the cove: a broad warm pool behind the instrument, so the ivory falls from light
             # behind the product to shade at the frame's edges (no flat catalogue field)
             light("Cove light", [0.0, 0.90, 1.40], [0.0, 0.85, 0.0], [0.9, 0.9], 40, WARM, spread=55, link=None)],
            [{"name": "Flag left", "mode": "negative", "position": [-0.55, -0.45, 0.10], "target": [0, 0, 0], "size": [0.9, 0.6]},
             {"name": "Flag right", "mode": "negative", "position": [0.55, -0.45, 0.10], "target": [0, 0, 0], "size": [0.9, 0.6]}]),
      world=0.05, exposure=-0.2, typography={"layout": "H01_C"},
      rationale="The website hero: the same square-on ivory portrait, the instrument lower and smaller so the upper third is free for the headline and the call to action, with the wordmark behind the instrument as a quiet architectural layer rather than the headline.")

for s in frames:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(frames), "hero specs ->", OUT)
