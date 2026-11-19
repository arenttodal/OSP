#!/usr/bin/env python3
"""Motion Gate R2: the four motion drafts developed from the approved style frames (Art Direction
Gate R2 + the R2 creative approval). Writes cine/shots/R01_MON, R02_POOL, R03_ENG, R04_WIN (.json).

  R01_MON   Grand Reveal: Monument     C1's atmosphere -> A2's three-quarter, the complete instrument;
                                       the wordmark (T1 / T2 / T3) uncovered by product + camera
  R02_POOL  Silhouette                 C2's pool, softened; the light carries the shot, camera minimal
  R03_ENG   Immersive Flyover          D1's perspective and light; a lateral track across all three engines
  R04_WIN   The Window Light           B3 coming to life: a very slow descent, a moving window light

Principles (from the review): every shot has its own movement curve; the light arrives before the
camera; nothing is complete at the midpoint (each shot's last third still develops); meaningful
opening, intermediate and final compositions; no cuts, particles, flares or gratuitous rotation.
Coordinates: product frame (metres from the instrument's centre at rest; x right along the keys,
y toward the back, z up). In R01_MON the instrument stands on the dark plank floor (top near z -0.086).
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "shots")
THEATRE_LIGHTS = ["Key softbox", "Rim strip", "Top line", "Front kicker", "Fill", "Floor overhead", "Floor rake"]
GALLERY_LIGHTS = ["Key softbox", "Edge strip", "Overhead", "Fill", "Front kicker", "Low rake"]
WARM, NEUTRAL, COOL = [1.0, 0.95, 0.88], [1.0, 0.975, 0.95], [0.93, 0.96, 1.0]
DISPLAY_FLAG = {"name": "Display flag", "mirror": True, "aim": [0.0, 0.065, 0.012], "size": [0.9, 0.6], "distance": 1.0}
CYC = {"rgb": [0.60, 0.565, 0.52], "roughness": 0.9, "radius": 1.4, "wall": 2.2, "height": 5.0, "width": 14.0}

# shot-specific curves (CSS cubic beziers)
REVEAL = [0.62, 0.0, 0.30, 1.0]     # R01: long to leave the corner, still travelling in the last third, a short quiet arrival
TURN = [0.40, 0.0, 0.25, 1.0]       # R01: the instrument's turn, finishing last
LIGHT_UP = [0.50, 0.0, 0.40, 1.0]   # light rising: hesitant, then sure
TRAVEL = [0.45, 0.0, 0.55, 1.0]     # R02: the strip's passage, even in the middle
DRIFT = [0.30, 0.0, 0.70, 1.0]      # R02: the camera's barely-there push
FLY = [0.38, 0.0, 0.62, 1.0]        # R03: a continuous glide, gentle at both ends


def orbit(az, el, d, target=(0, 0, 0)):
    a, e = math.radians(az), math.radians(el)
    return [round(target[0] + d * math.cos(e) * math.sin(a), 4), round(target[1] - d * math.cos(e) * math.cos(a), 4),
            round(target[2] + d * math.sin(e), 4)]


def light(name, position, target, size, power, rgb=NEUTRAL, spread=180, link="product"):
    return {"name": name, "position": position, "target": target, "size": size, "power": power, "rgb": rgb,
            "spread": spread, "link": link}


shots = []

# --------------------------------------------------------------------- R01 Grand Reveal: Monument (12 s)
# Opening: C1's view of the front-left corner (teak cheek, PITCH/MOD, keys, first macros), here seen with
# the instrument turned +30 deg, from 20 deg above (never the thin low slab). Closing: style frame A2,
# the complete instrument in three-quarter. The instrument turns back to 0 while the camera withdraws,
# climbs and travels right: product and camera share the move, so the depth never flattens.
A2_END = orbit(38, 34, 1.05, (0.0, 0.0, -0.09))
OPEN_POS, OPEN_TGT = [-0.135, -0.427, 0.036], [-0.062, -0.093, -0.088]
shots.append({
    "id": "R01_MON", "family": "R01 Grand Reveal", "variant": "Monument",
    "narrative": "Darkness, and the corner of the instrument close by: a rim of light on the teak cheek, a strip "
                 "skimming PITCH, MOD and the first keys - C1's world. The light arrives first: the key rises across "
                 "the ceramic, the long top sweep follows. Only then does the camera begin to withdraw, climb and travel "
                 "right while the instrument turns on its own axis toward its rest, so the depth never flattens. The "
                 "wordmark waits in the dark behind it and is uncovered by that coordinated movement; the displays wake "
                 "after the light. In the last seconds the complete instrument settles in A2's three-quarter, the "
                 "wordmark resolved beyond its far corner.",
    "setup": "ANDOR_FLOATING", "base_shot": "B1_R4", "aspect": [16, 9], "seconds": 12.0, "handles": 1.0,
    "camera": {"lens": 65, "fstop": 9,
               "position": [[0.0, OPEN_POS, "hold"], [1.6, OPEN_POS, REVEAL], [11.6, A2_END]],
               "target": [[0.0, OPEN_TGT, "hold"], [1.6, OPEN_TGT, REVEAL], [11.6, [-0.01, 0.0, -0.09]]],
               "focus": [[0.0, [-0.10, -0.15, -0.088], "hold"], [1.6, [-0.10, -0.15, -0.088], REVEAL], [11.6, [-0.01, 0.0, -0.09]]]},
    "product": {"pivot": [0, 0, 0], "translate": [0.0, 0.0, -0.098],
                "turn": [[0.0, 30.0, "hold"], [2.2, 30.0, TURN], [11.8, 0.0]]},
    "stage": {"remove_lights": ["Fill", "Front kicker", "Floor overhead"], "flags": [DISPLAY_FLAG],
              "add_lights": [light("Monument sweep", [0.75, 0.35, 0.70], [0.0, 0.0, 0.0], [1.4, 0.30], 22),
                             light("Corner strip", [-0.20, -0.48, 0.05], [-0.10, -0.20, -0.09], [0.03, 0.6], 0.08, WARM, spread=50)]},
    "lights": {"Rim strip": {"energy": [[0.0, 0.08, LIGHT_UP], [5.0, 1.0]]},
               "Corner strip": {"energy": [[0.0, 1.0, "hold"], [3.0, 1.0, "smooth"], [8.0, 0.0]]},
               "Key softbox": {"energy": [[0.0, 0.0, "hold"], [0.8, 0.0, LIGHT_UP], [5.0, 1.0]]},
               "Monument sweep": {"energy": [[0.0, 0.0, "hold"], [1.5, 0.0, LIGHT_UP], [6.5, 1.0]]},
               "Top line": {"energy": [[0.0, 0.0, "hold"], [3.0, 0.0, LIGHT_UP], [8.5, 0.35]]},
               "Floor rake": {"energy": [[0.0, 0.0, "hold"], [4.0, 0.0, LIGHT_UP], [10.5, 0.45]]}},
    "world": [[0.0, 0.05, LIGHT_UP], [7.0, 1.0]],
    "emission": [[0.0, 0.0, "hold"], [3.5, 0.0, LIGHT_UP], [9.0, 0.9]],
    "typography": {"style_layouts": {"T1": ["SF_A2", "T1"], "T2": ["SF_A2", "T2"], "T3": ["SF_A2", "T3"]},
                   "fade": [4.5, 10.5], "blur_from": 14.0},
    "notes": "Closing pose = style frame SF_A2 (camera, lens, lights). Each wordmark treatment is its own pass, "
             "composited through the instrument's matte on every frame; the clean master carries no type.",
})

# --------------------------------------------------------------------- R02 Silhouette: the pool (9 s)
# C2's camera and light, the pool softened (larger source, wider spread, -30 % at its centre) so the
# ceramic texture, the knob flutes and the labels survive. The camera only breathes (3 % closer).
C2_POS, C2_TGT = orbit(18, 44, 0.80, (-0.05, -0.05, 0.0)), [-0.04, -0.03, 0.0]
C2_END = orbit(18, 44, 0.775, (-0.05, -0.05, 0.0))
shots.append({
    "id": "R02_POOL", "family": "R02 Silhouette", "variant": "The pool of light",
    "narrative": "Darkness, a faint rim. A narrow strip of light drifts in from the left and crosses the controls "
                 "- PITCH and MOD, the keys, the six macros - each catching it in turn. As it passes, a pool begins "
                 "to gather above the macro row: small and dim at first, widening and warming until the controls sit "
                 "in one soft circle of light, the labels readable, the displays glowing faintly beyond. The camera "
                 "only breathes; the light carries the shot, and it is complete only at the end.",
    "setup": "ANDOR_FLOATING", "base_shot": "B1_R4", "aspect": [16, 9], "seconds": 9.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 8,
               "position": [[0.0, C2_POS, DRIFT], [9.0, C2_END]], "target": [[0.0, C2_TGT]]},
    "stage": {"remove_lights": [n for n in THEATRE_LIGHTS if n != "Rim strip"], "flags": [DISPLAY_FLAG],
              "add_lights": [light("Strip", [-0.42, -0.30, 0.14], [-0.20, -0.06, 0.0], [0.03, 0.6], 0.5, WARM, spread=40),
                             light("Pool", [-0.08, -0.08, 0.70], [-0.06, -0.06, 0.0], [0.14, 0.14], 0.6, WARM, spread=16)]},
    "lights": {"Rim strip": {"energy": [[0.0, 0.08, LIGHT_UP], [3.0, 0.15]]},
               "Strip": {"energy": [[0.0, 0.0, LIGHT_UP], [1.2, 1.0, "hold"], [5.0, 1.0, "smooth"], [7.5, 0.0]],
                         "translate": [[0.0, [0.0, 0.0, 0.0], "hold"], [0.6, [0.0, 0.0, 0.0], TRAVEL], [6.8, [0.36, 0.0, 0.0]]]},
               "Pool": {"energy": [[0.0, 0.0, "hold"], [3.4, 0.0, [0.55, 0.0, 0.35, 1.0]], [8.7, 1.0]],
                        "translate": [[0.0, [0.0, 0.0, -0.14], "hold"], [3.4, [0.0, 0.0, -0.14], [0.4, 0.0, 0.3, 1.0]], [8.7, [0.0, 0.0, 0.0]]]}},
    "world": 0.03, "exposure": 0.0,
    "emission": [[0.0, 0.0, "hold"], [5.5, 0.0, LIGHT_UP], [8.8, 0.25]],
    "notes": "Closing pose = style frame SF_C2, softened: Pool 0.14 m source, 16 deg spread, 0.6 W (C2: 0.10 m, 14 deg, "
             "1.0 W). The pool rises from 0.56 m to 0.70 m as it brightens, so it widens as it forms.",
})

# --------------------------------------------------------------------- R03 Immersive Flyover: the three engines (10 s)
# D1's height, angle and light, translated (not orbited) along the instrument from source A to C:
# position and target move in parallel, so the view direction stays D1's while the landscape passes.
shots.append({
    "id": "R03_ENG", "family": "R03 Above the Sound", "variant": "Immersive flyover: the three sound engines",
    "narrative": "We travel low along the instrument, as if over a landscape at evening: the knobs of the source "
                 "cards pass close and soft in the foreground, the three displays stand sharp beyond them - A's warm "
                 "waveform, B's, then C's - while the long shadows of the raking light run with us. The direction of "
                 "view never changes; only the instrument passes. The third engine arrives as the movement settles.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 10.0, "handles": 1.0,
    "camera": {"lens": 50, "fstop": 8,
               "position": [[0.0, [-0.25, -0.17, 0.095], FLY], [10.0, [0.12, -0.17, 0.105]]],
               "target": [[0.0, [-0.17, 0.06, 0.0], FLY], [10.0, [0.20, 0.06, 0.0]]],
               "focus": [[0.0, [-0.15, 0.06, 0.01], FLY], [10.0, [0.15, 0.06, 0.01]]]},
    "stage": {"hide_floor": True, "cyc": CYC, "remove_lights": GALLERY_LIGHTS, "flags": [DISPLAY_FLAG],
              "add_lights": [light("Rake", [-0.90, 0.55, 0.13], [0.0, 0.0, 0.0], [0.9, 0.15], 60, WARM, link=None),
                             light("Sky", [0.60, -0.60, 0.50], [0.0, 0.0, 0.0], [1.2, 1.2], 3, COOL)]},
    "world": 0.10, "exposure": -0.3,
    "notes": "Light = style frame SF_D1; f/8 (D1: f/5.6) so more of the functional surface stays readable. The display "
             "flag follows the camera every frame. Static displays here; the performance version uses Virtual Studio frames.",
})

# --------------------------------------------------------------------- R04 The Window Light (9 s)
# B3, coming to life: the camera descends 10 % over nine seconds; the window light drifts a little
# along the wall, so the long shadow turns and the sheen moves across the ceramic. The instrument is
# still; displays and controls are left free for the performance-driven pass.
B3_POS, B3_END = orbit(-8, 70, 1.60), orbit(-8, 70, 1.44)
shots.append({
    "id": "R04_WIN", "family": "R04 The Window Light", "variant": "B3 coming to life",
    "narrative": "The editorial photograph, breathing. From high above, the instrument lies turned in a field of warm "
                 "light; a window we never see lays its long shadow across the floor. Imperceptibly the camera sinks "
                 "toward it, and the window light drifts as an afternoon does: the shadow turns a few degrees, the sheen "
                 "slides across the ceramic and the knobs' small shadows lengthen. Nothing else moves - until, in the "
                 "performance version, the waveforms and controls begin to play.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 9.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 11,
               "position": [[0.0, B3_POS, [0.30, 0.0, 0.70, 1.0]], [9.0, B3_END]],
               "target": [[0.0, [0.06, -0.02, 0.0]]], "shift": [-0.08, 0.0]},
    "product": {"turn": 22.0},
    "stage": {"hide_floor": True, "cyc": CYC, "remove_lights": GALLERY_LIGHTS, "flags": [DISPLAY_FLAG,
              {"name": "Negative fill", "mode": "negative", "position": [0.65, -0.1, 0.35], "target": [0, 0, 0], "size": [1.2, 1.2]}],
              "add_lights": [light("Window", [-1.30, 0.40, 0.55], [0.0, 0.0, 0.0], [1.0, 1.6], 120, WARM, link=None)]},
    "lights": {"Window": {"translate": [[0.0, [0.0, -0.10, 0.0], "linear"], [9.0, [0.0, 0.14, -0.04]]],
                          "energy": [[0.0, 0.95, "linear"], [9.0, 1.0]]}},
    "world": 0.15, "exposure": -0.25,
    "notes": "Opening pose = style frame SF_B3. The window moves 24 cm along the wall and sinks 4 cm over the shot "
             "(linear: a sun does not ease). Displays are flagged and left static for the performance pass.",
})

for s in shots:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(shots), "motion specs ->", os.path.normpath(OUT))
