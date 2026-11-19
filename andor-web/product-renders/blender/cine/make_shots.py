#!/usr/bin/env python3
"""Writes the R01-R03 shot specs (first creative gate) into cine/shots/. Run after editing.

Product frame: metres from the instrument's centre at rest; x along the keys (right), y toward the
back, z up. The instrument is 0.40 m wide, 0.27 m deep and 27 mm tall in scene units. Visible
width at distance d with focal length L (36 mm sensor): W = 36 / L * d.
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shots")
os.makedirs(OUT, exist_ok=True)
SLOW_SETTLE = [0.45, 0.0, 0.25, 1.0]   # a slow start and a long, quiet settle (the house curve): still moving at 3/4
GENTLE = [0.33, 0.0, 0.25, 1.0]
GALLERY_LIGHTS = ["Key softbox", "Edge strip", "Overhead", "Fill", "Front kicker", "Low rake"]
THEATRE_LIGHTS = ["Key softbox", "Rim strip", "Top line", "Front kicker", "Fill", "Floor overhead", "Floor rake"]
shots = []

# ---------------------------------------------------------------- R01 GRAND REVEAL (11 s + handles)
shots.append({
    "id": "R01_A", "family": "R01 Grand Reveal", "variant": "A: ivory, editorial",
    "narrative": "We begin inside the instrument's world: a low, close oblique across the timber cheek and the "
                 "first card, more than the frame can hold. The camera withdraws in one unhurried breath while "
                 "the instrument lifts its face toward us; the light slides across the ceramic as it turns. The "
                 "product settles low in a calm, centred composition and the ANDOR wordmark, until now cut by "
                 "the instrument, stands complete behind it for the hold.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 11.0, "handles": 1.0,
    "camera": {"lens": 65, "fstop": 7.1,
               "position": [[0.0, [-0.30, -0.42, 0.05], SLOW_SETTLE], [10.0, [0.0, -0.96, 0.40]]],
               "target": [[0.0, [-0.04, 0.0, 0.015], SLOW_SETTLE], [10.0, [0.0, 0.05, 0.095]]],
               "focus": [[0.0, [-0.08, -0.04, 0.015], SLOW_SETTLE], [10.0, [0.0, 0.0, 0.03]]]},
    "product": {"pivot": [0, 0, 0], "tilt": [[0.0, 0.0, SLOW_SETTLE], [10.4, 18.0]],
                "translate": [[0.0, [0, 0, 0.035], SLOW_SETTLE], [10.4, [0, 0, 0.052]]]},
    "lights": {"Key softbox": {"orbit": [[0.0, -14.0, GENTLE], [11.0, 4.0]]}},
    "typography": {"wordmark": "ANDOR", "behind_product": True},
    "notes": "Floats 3.5-5 cm above the ivory floor so the tilt never meets it; the contact shadow is rendered every frame.",
})
shots.append(dict(shots[-1], id="R01_A_desktop", variant="A: desktop website composition (headline space left)",
                  narrative="R01 A, ending with the instrument right of centre and quiet space on the left for the "
                            "headline (One sound. An entire instrument.) beside the wordmark.",
                  camera=dict(shots[-1]["camera"], position=[[0.0, [-0.30, -0.42, 0.05], SLOW_SETTLE], [10.0, [0.08, -1.32, 0.53]]],
                              target=[[0.0, [-0.04, 0.0, 0.015], SLOW_SETTLE], [10.0, [-0.16, 0.05, 0.10]]])))
shots.append({
    "id": "R01_B", "family": "R01 Grand Reveal", "variant": "B: dark theatre, sculptural",
    "narrative": "Near darkness. A rim of light traces the instrument's front edge from just below its eye line. "
                 "As the camera draws back and slightly up, the instrument rises a few centimetres and tilts toward "
                 "us; the rim widens into a sculpted silhouette and the control surface catches the top light. "
                 "It settles floating in the dark, the wordmark emerging behind it.",
    "setup": "ANDOR_FLOATING", "base_shot": "B1_R4", "aspect": [16, 9], "seconds": 11.0, "handles": 1.0,
    "camera": {"lens": 75, "fstop": 8.0,
               "position": [[0.0, [0.30, -0.40, 0.012], SLOW_SETTLE], [10.0, [0.036, -1.20, 0.28]]],
               "target": [[0.0, [0.07, 0.0, 0.004], SLOW_SETTLE], [10.0, [-0.004, 0.03, 0.075]]]},
    "product": {"pivot": [0, 0, 0], "tilt": [[0.0, 0.0, SLOW_SETTLE], [10.4, 22.0]],
                "translate": [[0.0, [0, 0, -0.02], SLOW_SETTLE], [10.4, [0, 0, 0.03]]]},
    "lights": {"Key softbox": {"energy": [[0.0, 0.15, GENTLE], [8.0, 1.0]]},
               "Top line": {"energy": [[0.0, 0.3, GENTLE], [7.0, 1.0]]},
               "Fill": {"energy": [[0.0, 0.0, GENTLE], [9.0, 1.0]]}},
    "typography": {"wordmark": "ANDOR", "behind_product": True},
})
shots.append({
    "id": "R01_C", "family": "R01 Grand Reveal", "variant": "C: architectural scale",
    "narrative": "A low camera, almost at the instrument's level, close enough that the control surface is a "
                 "landscape. The camera withdraws along the floor and only gently rises, so the instrument stays "
                 "monumental - large in frame, seen slightly from below - while it tilts toward us and the wordmark "
                 "towers behind it.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 11.0, "handles": 1.0,
    "camera": {"lens": 50, "fstop": 8.0,
               "position": [[0.0, [0.08, -0.40, 0.022], SLOW_SETTLE], [10.0, [0.0, -0.70, 0.075]]],
               "target": [[0.0, [0.02, 0.0, 0.02], SLOW_SETTLE], [10.0, [0.0, 0.02, 0.07]]]},
    "product": {"pivot": [0, 0, 0], "tilt": [[0.0, 0.0, SLOW_SETTLE], [10.4, 20.0]],
                "translate": [[0.0, [0, 0, 0.035], SLOW_SETTLE], [10.4, [0, 0, 0.05]]]},
    "lights": {"Key softbox": {"orbit": [[0.0, 10.0, GENTLE], [11.0, -4.0]]}},
    "typography": {"wordmark": "ANDOR", "behind_product": True},
})

# ---------------------------------------------------------------- R02 SILHOUETTE (8 s + handles)
dark = lambda names, keep: {n: {"energy": [[0.0, 0.0]]} for n in names if n not in keep}
r02a_lights = dark(THEATRE_LIGHTS, ["Rim strip", "Top line", "Key softbox", "Fill", "Floor rake"])
r02a_lights.update({"Rim strip": {"energy": [[0.0, 0.3, GENTLE], [3.0, 1.0]]},
                    "Top line": {"energy": [[0.0, 0.0, "hold"], [3.5, 0.0, GENTLE], [7.0, 0.9]]},
                    "Key softbox": {"energy": [[0.0, 0.0, "hold"], [4.5, 0.0, GENTLE], [8.0, 0.55]]},
                    "Fill": {"energy": [[0.0, 0.0, "hold"], [5.0, 0.0, GENTLE], [8.0, 0.6]]},
                    "Floor rake": {"energy": [[0.0, 0.0, "hold"], [3.0, 0.0, GENTLE], [8.0, 0.35]]}})
shots.append({
    "id": "R02_A", "family": "R02 Silhouette", "variant": "A: dark theatre, travelling strip",
    "narrative": "Only the instrument's edge exists: a thin rim line against black. A narrow strip of light "
                 "travels slowly from the timber cheek along the front, and in its passing the ceramic's curvature, "
                 "the walnut grain and finally the control surface appear. The camera does not move; the light "
                 "tells the story.",
    "setup": "ANDOR_FLOATING", "base_shot": "B1_R4", "aspect": [16, 9], "seconds": 8.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 9.0,
               "position": [[0.0, [0.80, -0.90, 0.17], "linear"], [8.0, [0.79, -0.89, 0.169]]],
               "target": [[0.0, [0.0, 0.0, 0.035]]]},
    "lights": r02a_lights, "world": [[0.0, 0.1, "hold"], [4.0, 0.2, GENTLE], [8.0, 0.8]],
    "emission": [[0.0, 0.0, "hold"], [4.5, 0.0, GENTLE], [8.0, 0.7]],
    "cards": [{"name": "Travelling strip", "size": [0.025, 0.55], "color": [1.0, 0.97, 0.92],
               "position": [[0.0, [-0.45, -0.42, 0.28], [0.5, 0.0, 0.5, 1.0]], [7.0, [0.45, -0.42, 0.28]]],
               "target": [[0.0, [-0.12, 0.0, 0.0], [0.5, 0.0, 0.5, 1.0]], [7.0, [0.12, 0.0, 0.0]]],
               "strength": [[0.0, 0.0, "smooth"], [0.8, 40.0, "linear"], [6.5, 40.0, "smooth"], [8.0, 18.0]], "diffuse": True}],
})
r02b_lights = {n: {"energy": [[0.0, 0.0, "hold"], [0.8, 0.0, [0.55, 0.0, 0.2, 1.0]], [7.0, 1.0]]} for n in GALLERY_LIGHTS}
r02b_lights["Overhead"] = {"energy": [[0.0, 0.0, "hold"], [1.5, 0.0, [0.55, 0.0, 0.2, 1.0]], [7.0, 0.55]]}
r02b_lights["Low rake"] = {"energy": [[0.0, 0.7, GENTLE], [8.0, 1.0]], "orbit": [[0.0, -25.0, GENTLE], [8.0, 0.0]]}
shots.append({
    "id": "R02_B", "family": "R02 Silhouette", "variant": "B: ivory, the light rises",
    "narrative": "Dusk in the gallery. A single low raking light skims the floor and draws the instrument's "
                 "silhouette and timber edge; as it swings round, the room's soft architectural light rises "
                 "behind it, and ceramic, grain and controls resolve in warm daylight. An almost imperceptible "
                 "push forward.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 8.0, "handles": 1.0,
    "camera": {"lens": 70, "fstop": 8.0,
               "position": [[0.0, [-0.46, -0.86, 0.21], "linear"], [8.0, [-0.445, -0.835, 0.205]]],
               "target": [[0.0, [0.0, 0.01, 0.01]]]},
    "lights": r02b_lights, "exposure": [[0.0, -1.0, [0.55, 0.0, 0.2, 1.0]], [7.0, -0.25]],
    "world": [[0.0, 0.05, [0.55, 0.0, 0.2, 1.0]], [7.0, 1.0]],
    "emission": [[0.0, 0.0, "hold"], [2.0, 0.0, GENTLE], [7.5, 0.8]],
})
r02c_lights = {n: {"energy": [[0.0, 0.03, "hold"], [5.5, 0.03, GENTLE], [8.0, 0.7]]} for n in GALLERY_LIGHTS}
shots.append({
    "id": "R02_C", "family": "R02 Silhouette", "variant": "C: highlight scan over the control surface",
    "narrative": "A dim, close three-quarter view of the control surface. A broad soft reflection - a card "
                 "out of frame - glides across it, and each knob, key and display bezel flares into form as the "
                 "highlight crosses it, leaving the surface revealed in its wake. Locked camera.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 8.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 9.0,
               "position": [[0.0, [-0.42, -0.80, 0.55]]], "target": [[0.0, [0.0, 0.03, 0.0]]]},
    "lights": r02c_lights, "exposure": [[0.0, -0.5, "hold"], [5.5, -0.5, GENTLE], [8.0, 0.0]], "world": [[0.0, 0.05, "hold"], [5.5, 0.1, GENTLE], [8.0, 0.8]],
    "emission": [[0.0, 0.15, "hold"], [5.0, 0.15, GENTLE], [8.0, 0.8]],
    "cards": [{"name": "Scan card", "size": [0.14, 0.9], "color": [1.0, 0.98, 0.95],
               "position": [[0.0, [-0.75, 0.55, 0.45], [0.45, 0.0, 0.55, 1.0]], [7.0, [0.75, 0.55, 0.45]]],
               "target": [[0.0, [-0.1, 0.0, 0.0], [0.45, 0.0, 0.55, 1.0]], [7.0, [0.1, 0.0, 0.0]]],
               "strength": [[0.0, 0.0, "smooth"], [0.6, 30.0, "linear"], [6.8, 30.0, "smooth"], [8.0, 12.0]], "diffuse": False}],
})

# ---------------------------------------------------------------- R03 ABOVE THE SOUND (9 s + handles)
shots.append({
    "id": "R03_A", "family": "R03 Above the Sound", "variant": "A: diagonal flyover, rising",
    "narrative": "We drift diagonally above the three sound engines, from the near-left corner toward the far "
                 "right, rising slowly. The three cards, their displays and source controls pass beneath in "
                 "layered parallax, with a corner of the macro row anchoring the foreground: three voices, one "
                 "instrument.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 9.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 11.0,
               "position": [[0.0, [-0.55, -0.77, 0.60], GENTLE], [9.0, [0.34, -0.70, 0.88]]],
               "target": [[0.0, [-0.06, 0.04, 0.0], GENTLE], [9.0, [0.03, 0.055, 0.0]]]},
    "lights": {"Key softbox": {"orbit": [[0.0, -6.0, "linear"], [9.0, 6.0]]}},
})
shots.append({
    "id": "R03_B", "family": "R03 Above the Sound", "variant": "B: dark, lateral glide with descent",
    "narrative": "In the dark theatre, a measured lateral glide from right to left across the three engines, "
                 "the camera leading slightly ahead of its subject and descending a little, so the displays rise "
                 "in the frame as we pass. Rim light separates every control from the black.",
    "setup": "ANDOR_FLOATING", "base_shot": "B1_R4", "aspect": [16, 9], "seconds": 9.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 11.0,
               "position": [[0.0, [0.42, -0.80, 0.58], "in_out"], [9.0, [-0.42, -0.78, 0.44]]],
               "target": [[0.0, [0.08, 0.065, 0.0], "in_out"], [9.0, [-0.06, 0.065, 0.0]]]},
})
shots.append({
    "id": "R03_C", "family": "R03 Above the Sound", "variant": "C: crane forward over the keys",
    "narrative": "From a low front view across the keys, the camera cranes up and forward over the macro row "
                 "until it looks down onto the three engines; the point of interest moves from the keys to the "
                 "displays as the instrument's whole surface opens beneath us.",
    "setup": "ANDOR_GALLERY", "base_shot": "A2", "aspect": [16, 9], "seconds": 9.0, "handles": 1.0,
    "camera": {"lens": 85, "fstop": 11.0,
               "position": [[0.0, [0.02, -0.98, 0.20], SLOW_SETTLE], [9.0, [0.03, -0.52, 0.88]]],
               "target": [[0.0, [0.0, -0.06, 0.0], SLOW_SETTLE], [9.0, [0.0, 0.06, 0.0]]]},
})

for s in shots:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(shots), "shot specs ->", OUT)
