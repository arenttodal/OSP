#!/usr/bin/env python3
"""Motion R4: the refinements the R3 review asked for. Writes cine/shots/*_R4.json; the R3 specs
(make_motion_r3.py) stay as they were, for reproducibility.

  R01_CLOSE_R4     R01 shot 1, framed 9 cm further back on the instrument, so the far edge and the
                   theatre's dark run across the top of the frame as they do in the portrait (tonal
                   continuity across the cut). Same lens, same 3 cm drift, same 5.5 s.
  R01_PORTRAIT_R4  R01 shot 2, the A2 portrait eased out to 1.10x A2's distance so the whole
                   instrument sits in frame (R3 lost its front-right corner); the same 1.5 % settle.
                   Typography T1 only (the review's preference).
  R02_DIM_R4       R02 with the peak light 0.3 EV lower (x 2^-0.3 = 0.812 on the strip, the pool and
                   the display glow). The dim level, the movement and the timing are unchanged.
  B1_CLOSE_STUDY   B1, a closer medium-wide still from the approved B1 still's direction: the three
                   displays and their controls fill the frame; the glass keeps a narrow edge
                   highlight instead of a broad reflection.
"""
import copy
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "shots")
STYLE = os.path.join(HERE, "..", "style", "specs")
EV_DOWN = 2 ** -0.3


def load(path):
    return json.load(open(path))


def add(v, d):
    return [round(a + b, 4) for a, b in zip(v, d)]


def scale(v, k):
    return [round(a * k, 4) for a in v]


def shift_keys(channel, d):
    """a keyframed vector channel (or a plain vector) moved by d."""
    if channel and isinstance(channel[0], list):
        return [[k[0], add(k[1], d)] + k[2:] for k in channel]
    return add(channel, d)


shots = []

# ---------------------------------------------------------------- R01: the close shot, framed higher
s = load(os.path.join(OUT, "R01_CLOSE.json"))
s["id"], s["variant"] = "R01_CLOSE_R4", "Shot 1: close, drifting (R4: the far edge and the dark across the top)"
for k in ("position", "target", "focus"):
    s["camera"][k] = shift_keys(s["camera"][k], [0.0, 0.09, 0.01])
s["notes"] = "R4: camera, target and focus 9 cm further back on the instrument (+1 cm up): the frame's mean brightness falls from 135 to about 110, the ceramic's exposure is unchanged."
shots.append(s)

# ---------------------------------------------------------------- R01: the portrait, whole in frame
A2 = load(os.path.join(STYLE, "SF_A2.json"))
c = A2["camera"]
d = [p - t for p, t in zip(c["position"], c["target"])]
s = load(os.path.join(OUT, "R01_PORTRAIT.json"))
s["id"], s["variant"] = "R01_PORTRAIT_R4", "Shot 2: the A2 portrait with the wordmark (R4: whole instrument, T1)"
K = 1.10
SETTLE = [0.25, 0.0, 0.35, 1.0]
s["camera"] = dict(c, position=[[0.0, add(c["target"], scale(d, K * 0.985)), SETTLE], [6.5, add(c["target"], scale(d, K))]],
                   target=c["target"])
s["typography"] = {"style_layouts": {"T1": ["SF_A2", "T1"]}, "fade": [0.0, 0.0], "blur_from": 0.0}
s["notes"] = "R4: A2's direction at 1.10x its distance (the whole instrument in frame, with room at the front-right corner); 1.5 % settle as in R3; T1 only."
shots.append(s)

# ---------------------------------------------------------------- R02: the peak 0.3 EV lower
s = load(os.path.join(OUT, "R02_DIM.json"))
s["id"], s["variant"] = "R02_DIM_R4", "The pool of light, returning to dim (R4: peak -0.3 EV)"
for name in ("Strip", "Pool"):
    s["lights"][name]["energy"] = [[k[0], round(k[1] * EV_DOWN, 4)] + k[2:] for k in s["lights"][name]["energy"]]
s["emission"] = [[k[0], round(k[1] * EV_DOWN, 4)] + k[2:] for k in s["emission"]]
s["notes"] = ("R4: the strip, the pool and the display glow peak 0.3 EV lower (x 0.812); the dim level (Rim strip, world), "
              "the movement and the timing are R3's. Loop: every channel has the same value at 0 s and 11 s.")
shots.append(s)

# ---------------------------------------------------------------- B1: a closer study (one still)
B1 = load(os.path.join(STYLE, "SF_B1.json"))
s = copy.deepcopy(B1)
cam = s["camera"]
tgt = [0.0, 0.065, 0.012]                      # the display row's centre (the display flag's aim)
dirv = [p - t for p, t in zip(cam["position"], cam["target"])]
s.update({"id": "B1_CLOSE_STUDY", "family": "Editorial motion", "variant": "B1: closer medium-wide (study)",
          "seconds": 1.0, "handles": 0.0, "from_still": "SF_B1"})
s.pop("typography", None)
s["camera"] = {"lens": 85, "fstop": 8, "position": add(tgt, scale(dirv, 0.68)), "target": tgt, "focus": tgt}
for L in s["stage"]["add_lights"]:
    if L["name"] == "Edge strip":
        # R3's broad reflection on display C came from this strip: lower and narrower, it keeps a thin
        # highlight along the glass's far edge and the knobs' rims
        L.update({"position": [0.85, 0.55, 0.16], "size": [0.05, 0.5], "power": 7})
s["notes"] = "Study: B1's direction at 0.68x its distance, aimed at the display row; the edge strip lowered and narrowed to control the glass reflection."
shots.append(s)

for s in shots:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(shots), "R4 specs ->", os.path.normpath(OUT))
