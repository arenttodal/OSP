#!/usr/bin/env python3
"""Art Direction Gate R2: writes the 12 style-frame specs (four families x three) into style/specs/.

Each style frame is a cine_rig.py shot spec held still (one pose, rendered with --still 0), so
every frame can later become the opening, middle or closing pose of a motion shot unchanged.

Product frame: metres from the instrument's centre at rest; x along the keys (right), y toward
the back, z up. The instrument is 0.40 x 0.27 m, 27 mm tall. Landmarks (editor px -> metres):
displays y +0.035..+0.095; macro knobs x -0.165..+0.037 at y -0.065; modulation panel x
+0.064..+0.188; keys x -0.159..+0.158 at y -0.105..-0.13; PITCH/MOD x -0.19..-0.165.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "specs")
os.makedirs(OUT, exist_ok=True)

GALLERY_LIGHTS = ["Key softbox", "Edge strip", "Overhead", "Fill", "Front kicker", "Low rake"]
THEATRE_LIGHTS = ["Key softbox", "Rim strip", "Top line", "Front kicker", "Fill", "Floor overhead", "Floor rake"]
WARM, NEUTRAL, COOL = [1.0, 0.95, 0.88], [1.0, 0.975, 0.95], [0.93, 0.96, 1.0]
CYC = {"rgb": [0.60, 0.565, 0.52], "roughness": 0.9, "radius": 1.4, "wall": 2.2, "height": 5.0, "width": 14.0}
# the displays' own reflection of the studio, replaced by black (a flag where the glass looks)
DISPLAY_FLAG = {"name": "Display flag", "mirror": True, "aim": [0.0, 0.065, 0.012], "size": [0.9, 0.6], "distance": 1.0}


def sweep(aim, size, strength, distance=1.2, offset=(0.0, 0.0), name="Sweep"):
    """a long softbox seen only in the ceramic: its highlight lands on `aim` (mirror placement)."""
    return {"name": name, "size": size, "strength": strength, "color": [1.0, 0.975, 0.95],
            "mirror": {"aim": aim, "distance": distance, "offset": list(offset)}, "link": "product"}


def orbit(az, el, d, target=(0, 0, 0)):
    """camera position: az degrees from the front (-y) toward the right (+x), el above horizontal."""
    a, e = math.radians(az), math.radians(el)
    return [round(target[0] + d * math.cos(e) * math.sin(a), 4), round(target[1] - d * math.cos(e) * math.cos(a), 4),
            round(target[2] + d * math.sin(e), 4)]


def light(name, position, target, size, power, rgb=NEUTRAL, spread=180, link="product", glossy_only=False):
    return {"name": name, "position": position, "target": target, "size": size, "power": power, "rgb": rgb,
            "spread": spread, "link": link, "glossy_only": glossy_only}


frames = []


def frame(fid, family, title, setup, base, cam, stage, rationale="", **kw):
    s = {"id": fid, "family": family, "variant": title, "setup": setup, "base_shot": base, "aspect": [16, 9],
         "seconds": 1.0, "handles": 0.0, "camera": cam, "stage": stage, "rationale": rationale}
    s.update(kw)
    frames.append(s)


# ------------------------------------------------------------------------------ A  MONUMENT (dark)
# The dark theatre of R01 B, the instrument grounded on the walnut floor (not floating): mass.
A_STAGE = {"remove_lights": ["Fill", "Front kicker", "Floor overhead"], "flags": [DISPLAY_FLAG]}
A_DIM = {"Floor rake": {"energy": 0.45}, "Top line": {"energy": 0.35}}
A_LIGHTS = lambda extra: [light("Monument sweep", [0.75, 0.35, 0.70], [0.0, 0.0, 0.0], [1.4, 0.30], 22, NEUTRAL)] + extra
GROUND = {"translate": [0.0, 0.0, -0.098]}
frame("SF_A1", "A Monument", "Diagonal monolith", "ANDOR_FLOATING", "B1_R4",
      {"lens": 50, "fstop": 8, "position": orbit(-40, 21, 0.70, (0.05, 0.0, -0.085)), "target": [0.06, 0.02, -0.085],
       "focus": [-0.06, -0.05, -0.085], "shift": [0.0, -0.06]},
      dict(A_STAGE, add_lights=A_LIGHTS([])), product=dict(GROUND, turn=14.0), lights=A_DIM, exposure=0.0, emission=0.9,
      typography={"layout": "A1"},
      rationale="The instrument as a monolith on dark walnut: seen from its front-left corner and turned 14 degrees, its long edge becomes one strong diagonal that climbs from the lower left into the dark. The timber cheek anchors the frame; the far end dissolves. The wordmark stands in the darkness beyond, and the instrument's far edge cuts across its feet: the type is behind the object, not above it.")
frame("SF_A2", "A Monument", "Plinth from above", "ANDOR_FLOATING", "B1_R4",
      {"lens": 65, "fstop": 9, "position": orbit(38, 34, 1.05, (0.0, 0.0, -0.09)), "target": [-0.01, 0.0, -0.09]},
      dict(A_STAGE, add_lights=A_LIGHTS([])), product=GROUND, lights=A_DIM, exposure=0.0, emission=0.9,
      typography={"layout": "A2"},
      rationale='The plinth view: from above and to the right, the instrument floats as a single bright plane against the walnut and the shadowed floor beyond, its diagonal running against the planks. The quietest of the three, and the closest to a classic hero, but with mass and darkness on both sides.')
frame("SF_A3", "A Monument", "Corner monument", "ANDOR_FLOATING", "B1_R4",
      {"lens": 50, "fstop": 8, "position": orbit(42, 27, 0.52, (0.06, 0.0, -0.09)), "target": [0.02, 0.03, -0.09],
       "focus": [0.13, -0.08, -0.086], "shift": [0.0, -0.04]},
      dict(A_STAGE, add_lights=[light("Side key", [0.55, -0.20, 0.12], [0.10, -0.02, -0.09], [0.35, 0.25], 9, WARM)]),
      product=dict(GROUND, turn=-10.0), lights=dict(A_DIM, **{"Key softbox": {"energy": 0.35}, "Floor rake": {"energy": 0.0}}), exposure=0.0, emission=0.9,
      typography={"layout": "A3"},
      rationale='Confident scale: the camera stands over the front-right corner, so the ARP card, the modulation display and the third source card become architecture and the instrument fills the frame on a falling diagonal. A single side key models the controls; the far end and the floor go to black.')

# ------------------------------------------------------------------------------ B  SCULPTURE (ivory cyc)
B_STAGE = lambda lights, flags=(): {"hide_floor": True, "cyc": CYC, "remove_lights": GALLERY_LIGHTS,
                                    "add_lights": lights, "flags": [DISPLAY_FLAG] + list(flags)}
frame("SF_B1", "B Sculpture", "Ivory three-quarter", "ANDOR_GALLERY", "A2",
      {"lens": 85, "fstop": 11, "position": orbit(-30, 20, 1.10), "target": [0.02, 0.0, 0.0], "shift": [-0.04, -0.02]},
      B_STAGE([light("Key", [-0.90, 0.70, 0.45], [0.0, 0.0, 0.0], [1.8, 1.0], 55, WARM, link=None),
               light("Edge strip", [0.85, 0.55, 0.30], [0.0, 0.0, 0.0], [2.0, 0.10], 26, NEUTRAL, spread=90, link="product")],
              [{"name": "Negative fill", "mode": "negative", "position": [0.75, -0.35, 0.15], "target": [0, 0, 0], "size": [1.2, 0.8]}]),
      world=0.15, exposure=-0.45, cards=[sweep([0.0, -0.05, 0.012], [1.8, 0.30], 30.0)],
      rationale='The ivory portrait: a seamless warm cyclorama (no horizon line), one large low key from behind left so the ceramic turns from light to shade across its width, a black negative-fill card on the right to keep the right cheek in shadow, and a long soft shadow toward the viewer.')
frame("SF_B2", "B Sculpture", "Low profile, the curve", "ANDOR_GALLERY", "A2",
      {"lens": 85, "fstop": 11, "position": orbit(58, 12, 0.72), "target": [-0.03, 0.0, 0.0]},
      B_STAGE([light("Key", [0.10, 0.95, 0.85], [0.0, 0.0, 0.0], [2.2, 0.9], 45, WARM, link=None),
               light("Top strip", [0.30, -0.10, 0.75], [0.0, 0.0, 0.0], [2.2, 0.12], 10, NEUTRAL, spread=70, link="product")],
              [{"name": "Negative fill", "mode": "negative", "position": [0.55, -0.55, 0.12], "target": [0, 0, 0], "size": [1.4, 0.6]}]),
      world=0.15, exposure=-0.35, cards=[sweep([0.0, -0.06, 0.012], [1.8, 0.15], 40.0)],
      rationale="The curve: low over the right end, along the instrument's length. Every key front, knob skirt and the rounded ceramic edge catches the light from behind; the cyclorama falls from dark to warm grey behind it, so the silhouette reads against a gradient, not a line.")
frame("SF_B3", "B Sculpture", "Overhead, long light", "ANDOR_GALLERY", "A2",
      {"lens": 85, "fstop": 11, "position": orbit(-8, 70, 1.60), "target": [0.06, -0.02, 0.0], "shift": [-0.08, 0.0]},
      B_STAGE([light("Window", [-1.30, 0.40, 0.55], [0.0, 0.0, 0.0], [1.0, 1.6], 120, WARM, link=None)],
              [{"name": "Negative fill", "mode": "negative", "position": [0.65, -0.1, 0.35], "target": [0, 0, 0], "size": [1.2, 1.2]}]),
      product={"turn": 22.0}, world=0.15, exposure=-0.25,
      rationale='From above, turned off-axis: a single low window light from the left lays a long, soft shadow across the cyclorama, the instrument becomes a graphic object in a field of warm negative space. The displays are flagged so the waveforms read clean.')

# ------------------------------------------------------------------------------ C  SILHOUETTE (dark)
C_OFF = lambda keep: {"remove_lights": [n for n in THEATRE_LIGHTS if n not in keep], "flags": [DISPLAY_FLAG]}
frame("SF_C1", "C Silhouette", "Timber, keys and macros", "ANDOR_FLOATING", "B1_R4",
      {"lens": 85, "fstop": 8, "position": [-0.40, -0.38, 0.16], "target": [-0.10, -0.05, -0.005], "focus": [-0.17, -0.10, 0.0]},
      dict(C_OFF(["Rim strip"]),
           add_lights=[light("Strip", [-0.32, -0.40, 0.12], [-0.12, 0.0, 0.0], [0.03, 0.6], 0.8, WARM)]),
      lights={"Rim strip": {"energy": 0.25}}, world=0.05, emission=0.35, exposure=-1.3,
      rationale='After R02 A: almost everything is darkness. One narrow strip skims the front-left corner: the walnut cheek, the PITCH and MOD strips, the first macro knobs and the keys catch it; the rest of the instrument is suggested, not shown.')
frame("SF_C2", "C Silhouette", "A pool of light on the controls", "ANDOR_FLOATING", "B1_R4",
      {"lens": 85, "fstop": 8, "position": orbit(18, 44, 0.80, (-0.05, -0.05, 0.0)), "target": [-0.04, -0.03, 0.0]},
      dict(C_OFF([]), add_lights=[light("Pool", [-0.08, -0.08, 0.70], [-0.06, -0.06, 0.0], [0.10, 0.10], 1.0, WARM, spread=14)]),
      world=0.03, emission=0.25, exposure=0.0,
      rationale='A single pool of light falls on the macro row and the keys; the displays glow faintly beyond it, the rest is black. The pool is the subject: the instrument is played where the light is.')
frame("SF_C3", "C Silhouette", "The outline", "ANDOR_FLOATING", "B1_R4",
      {"lens": 85, "fstop": 9, "position": orbit(-28, 10, 0.75), "target": [0.0, 0.0, 0.0], "shift": [0.0, -0.08]},
      C_OFF(["Rim strip"]), world=0.03, emission=0.08, exposure=-0.2,
      rationale="The outline: only a rim behind the instrument. The knobs' silhouettes along the top edge and a hairline along the ceramic are enough to recognise it; below it, darkness as negative space.")

# ------------------------------------------------------------------------------ D  IMMERSION (ivory)
D_STAGE = lambda lights: {"hide_floor": True, "cyc": CYC, "remove_lights": GALLERY_LIGHTS, "add_lights": lights,
                          "flags": [DISPLAY_FLAG]}
# architectural light: a low raking key (12 deg) so every knob and key throws a long shadow across the surface
D_KEY = [light("Rake", [-0.90, 0.55, 0.13], [0.0, 0.0, 0.0], [0.9, 0.15], 60, WARM, link=None),
         light("Sky", [0.60, -0.60, 0.50], [0.0, 0.0, 0.0], [1.2, 1.2], 3, COOL, link="product")]
frame("SF_D1", "D Immersion", "The three sound engines", "ANDOR_GALLERY", "A2",
      {"lens": 50, "fstop": 5.6, "position": [-0.14, -0.15, 0.10], "target": [0.03, 0.06, 0.0], "focus": [0.0, 0.06, 0.01]},
      D_STAGE(D_KEY), world=0.10, exposure=-0.3,
      rationale="The three sound engines as a landscape: the camera travels low over the source cards, the near knobs soft in the foreground, the middle display sharp, the far one receding. A low raking light draws every knob's shadow across the ceramic.")
frame("SF_D2", "D Immersion", "The macro row", "ANDOR_GALLERY", "A2",
      {"lens": 85, "fstop": 5.6, "position": [0.14, -0.27, 0.07], "target": [-0.07, -0.06, 0.0], "focus": [-0.04, -0.065, 0.01]},
      D_STAGE(D_KEY), world=0.10, exposure=-0.3,
      rationale='The macro row as architecture: six knobs recede on a diagonal, each throwing a long evening shadow across the panel; the keys run beneath them out of focus. Close enough to be inside the instrument, far enough to read what it is.')
frame("SF_D3", "D Immersion", "Keys and expression", "ANDOR_GALLERY", "A2",
      {"lens": 50, "fstop": 8, "position": [-0.36, -0.24, 0.075], "target": [0.02, -0.07, 0.0], "focus": [-0.13, -0.115, 0.004]},
      D_STAGE(D_KEY), world=0.10, exposure=-0.3,
      rationale='From the left end, along the instrument: the walnut cheek, the PITCH and MOD strips and the keyboard in the foreground, the macro row and the source cards rising behind into darkness. The main surface extends away from us like a street.')

for s in frames:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(frames), "style frames ->", OUT)
