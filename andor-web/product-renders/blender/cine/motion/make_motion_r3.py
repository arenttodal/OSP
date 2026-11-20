#!/usr/bin/env python3
"""Motion Revision R3: restrained, photographic motion (the R04 language). Writes cine/shots/*.json.

  R01_CLOSE    R01, shot 1: a close view of the instrument at A2's angle; an extremely slow lateral drift
  R01_PORTRAIT R01, shot 2: a clean cut out to the A2 portrait, the wordmark behind (T1 / T2 / T3 passes)
  R02_DIM      R02: the light reveals the controls, then falls back to the opening's dim level (a loop)
  R03_GLIDE    R03: the lateral flyover at one constant, slow speed, so any section can be cut out
  R04_WIN16    R04: the same moves, retimed in the source animation from 9 s to 16 s
  B1_GLIDE     the relit B1 three-quarter, a slow lateral glide (camera-right, no yaw change)
  D2_TRACK     the relit D2 macro row, a slow lateral track (focus travels with the camera: no hunting)
  H01_B_DOLLY  H01 Ivory Gallery: a 4 % dolly back settling on the hero still (lightweight preview only)

What the review asked for, as rules here:
- no product rotation anywhere: every shot keeps its still's product pose;
- the camera translates; its direction of view never changes (position and target move together);
- moves are small and slow, eased only where a shot starts or settles on an approved still;
- B1, D2, A2 and H01 shots load the approved still specs, so they begin or end exactly on them.
The R2 specs (make_motion.py) are kept unchanged for reproducibility.
"""
import copy
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "shots")
STYLE = os.path.join(HERE, "..", "style", "specs")
HERO = os.path.join(HERE, "..", "hero", "specs")
SETTLE = [0.25, 0.0, 0.35, 1.0]     # a move that arrives on a still: leaves at once, settles long
EASE = [0.30, 0.0, 0.70, 1.0]       # R04's own camera curve
LIGHT_UP = [0.50, 0.0, 0.40, 1.0]
FALL = [0.45, 0.0, 0.60, 1.0]       # light returning to dim, as slowly as it came


def load(path):
    return json.load(open(path))


def add(v, d):
    return [round(a + b, 4) for a, b in zip(v, d)]


def scale(v, k):
    return [round(a * k, 4) for a in v]


def right_vector(cam):
    """the camera's horizontal right: perpendicular to its view direction, in the floor plane."""
    dx, dy = cam["target"][0] - cam["position"][0], cam["target"][1] - cam["position"][1]
    n = math.hypot(dx, dy)
    return [dy / n, -dx / n, 0.0]


def lateral(cam, seconds, metres, curve="linear", start=0.0):
    """the still's camera translated sideways by `metres` (centred on the still at the midpoint
    when start=0.5, starting on it when start=0.0); position, target and focus move together."""
    r = right_vector(cam)
    a, b = scale(r, -metres * start), scale(r, metres * (1 - start))
    out = dict(cam)
    for k in ("position", "target", "focus"):
        if k in cam:
            out[k] = [[0.0, add(cam[k], a), curve], [seconds, add(cam[k], b)]]
    return out


def from_still(spec, sid, family, variant, seconds, narrative, handles=1.0):
    s = copy.deepcopy(spec)
    for k in ("rationale", "typography"):
        s.pop(k, None)
    s.update({"id": sid, "family": family, "variant": variant, "seconds": seconds, "handles": handles,
              "narrative": narrative, "from_still": spec["id"]})
    return s


shots = []

# --------------------------------------------------------------------- R01: two compositions and a cut
# Both on A2's axis (the camera looks at the instrument from the same direction), so the cut reads as
# a deliberate step back, not a jump. The instrument does not turn. Shot 1 is close and drifts 3 cm
# sideways in 5.5 s; shot 2 is the A2 portrait, holding with a 1.5 % dolly back while the wordmark
# sits behind it from the first frame of the cut.
A2 = load(os.path.join(STYLE, "SF_A2.json"))
c = A2["camera"]
d = [p - t for p, t in zip(c["position"], c["target"])]
close_tgt = [-0.035, -0.035, -0.09]
close = {"lens": 65, "fstop": 6.3, "position": add(close_tgt, scale(d, 0.50)), "target": close_tgt,
         "focus": [-0.06, -0.06, -0.086]}
s = from_still(A2, "R01_CLOSE", "R01 Monument (cut)", "Shot 1: close, drifting", 5.5,
               "Close on the instrument from A2's direction, at half the distance: the displays, the macros and the "
               "keys run diagonally through the frame in the theatre's warm dark, focus on the macro row. The camera "
               "drifts sideways by three centimetres; nothing else moves.")
s["camera"] = lateral(close, 5.5, 0.03)
shots.append(s)

s = from_still(A2, "R01_PORTRAIT", "R01 Monument (cut)", "Shot 2: the A2 portrait with the wordmark", 6.5,
               "Cut out to the portrait: the complete instrument at A2's three-quarter, the ANDOR wordmark standing in "
               "the dark behind it, the far corner crossing the foot of the letters. The camera eases back by 1.5 % and "
               "settles; the instrument and the type stay registered.")
s["camera"] = dict(c, position=[[0.0, add(c["target"], scale(d, 0.985)), SETTLE], [6.5, c["position"]]],
                   target=c["target"])
s["typography"] = {"style_layouts": {"T1": ["SF_A2", "T1"], "T2": ["SF_A2", "T2"], "T3": ["SF_A2", "T3"]},
                   "fade": [0.0, 0.0], "blur_from": 0.0}
shots.append(s)

# --------------------------------------------------------------------- R02: reveal, then back to dim
# R2's light, with the camera fixed and the ending returned to the opening: the strip crosses, the pool
# forms (peak 25 % lower than R2), then everything eases back to the dim level the shot opened on. The
# dim level is raised from R2's near-black (rim 0.08, world 0.03) so the ceramic and the knob flutes
# keep their shape in the shadows. First and last frame carry the same light: a natural loop.
R2 = load(os.path.join(OUT, "R02_POOL.json"))
s = copy.deepcopy(R2)
DIM_RIM, DIM_WORLD = 0.14, 0.045
pos = R2["camera"]["position"][0][1]
s.update({"id": "R02_DIM", "variant": "The pool of light, returning to dim", "seconds": 11.0,
          "narrative": "Dim: the ceramic and the knobs are only just there, a faint rim along the edge. A narrow strip "
                       "of light crosses the controls; behind it a soft pool gathers over the macro row and the labels "
                       "come up into the light. Then, as slowly as it came, the light withdraws and the instrument "
                       "returns to the dim it began in - shape still visible, nothing black. The camera does not move; "
                       "the last frame is the first."})
s["camera"] = {"lens": 85, "fstop": 8, "position": [[0.0, pos]], "target": R2["camera"]["target"]}
s["lights"] = {"Rim strip": {"energy": [[0.0, DIM_RIM]]},
               "Strip": {"energy": [[0.0, 0.0, LIGHT_UP], [1.4, 0.85, "hold"], [4.2, 0.85, "smooth"], [6.4, 0.0]],
                         "translate": [[0.0, [0.0, 0.0, 0.0], "hold"], [0.6, [0.0, 0.0, 0.0], [0.45, 0.0, 0.55, 1.0]], [6.2, [0.36, 0.0, 0.0]]]},
               "Pool": {"energy": [[0.0, 0.0, "hold"], [2.6, 0.0, [0.55, 0.0, 0.35, 1.0]], [6.2, 0.75, FALL], [10.6, 0.0]],
                        "translate": [[0.0, [0.0, 0.0, -0.14], "hold"], [2.6, [0.0, 0.0, -0.14], [0.4, 0.0, 0.3, 1.0]], [6.2, [0.0, 0.0, 0.0]]]}}
s["world"] = DIM_WORLD
s["emission"] = [[0.0, 0.0, "hold"], [4.0, 0.0, LIGHT_UP], [6.4, 0.22, FALL], [10.6, 0.0]]
s["notes"] = ("Loop: every channel has the same value at 0 s and 11 s (the strip's travel and the pool's height move "
              "only while they are dark). Dim level: Rim strip x 0.14, world 0.045 (R2: 0.08 / 0.03).")
shots.append(s)

# --------------------------------------------------------------------- R03: one constant, slow speed
R3 = load(os.path.join(OUT, "R03_ENG.json"))
s = copy.deepcopy(R3)
s.update({"id": "R03_GLIDE", "variant": "Immersive flyover: constant glide", "seconds": 17.0,
          "narrative": "R03's lateral track over the three engines at one constant, slow speed (2.5 cm/s): no "
                       "acceleration, no settling, no change of direction, so any stretch can be cut out and used alone. "
                       "Sections: A 0-6.5 s, B 5.5-11.5 s, C 10.5-17 s (a second of overlap between each)."})
cam = s["camera"]
p0, p1 = cam["position"][0][1], cam["position"][-1][1]
t0, t1 = cam["target"][0][1], cam["target"][-1][1]
f0, f1 = cam["focus"][0][1], cam["focus"][-1][1]
ext = 0.025      # 2.5 cm more at each end, at the same speed, for the sections' heads and tails
dirn = [(b - a) / math.dist(p0, p1) for a, b in zip(p0, p1)]
span = lambda a, b: [[0.0, add(a, scale(dirn, -ext)), "linear"], [17.0, add(b, scale(dirn, ext))]]
s["camera"] = dict(cam, position=span(p0, p1), target=span(t0, t1), focus=span(f0, f1))
s["sections"] = {"A": [0.0, 6.5], "B": [5.5, 11.5], "C": [10.5, 17.0]}
shots.append(s)

# --------------------------------------------------------------------- R04: retimed in the source
R4 = load(os.path.join(OUT, "R04_WIN.json"))
s = copy.deepcopy(R4)
k = 16.0 / R4["seconds"]


def retime(ch):
    return [[round(kf[0] * k, 4)] + kf[1:] for kf in ch]


s.update({"id": "R04_WIN16", "variant": "B3 coming to life (16 s)", "seconds": 16.0,
          "narrative": R4["narrative"] + " Retimed from 9 to 16 s in the source animation: the same camera descent "
                       "and the same window travel, each keyframe moved, every frame rendered."})
s["camera"] = dict(R4["camera"], position=retime(R4["camera"]["position"]))
s["lights"] = {"Window": {ch: retime(v) for ch, v in R4["lights"]["Window"].items()}}
shots.append(s)

# --------------------------------------------------------------------- B1 and D2: lateral moves on the relit stills
B1 = load(os.path.join(STYLE, "SF_B1.json"))
s = from_still(B1, "B1_GLIDE", "Editorial motion", "B1 Three displays: lateral glide", 12.0,
               "The relit three-quarter, gliding: the camera slides 7 cm to its right over twelve seconds, centred on the "
               "approved still at the midpoint. Its direction of view never changes, so the controlled highlights "
               "travel slowly across the ceramic and the display glass while the waveforms stay readable (the display "
               "flag follows the camera).")
s["camera"] = lateral(B1["camera"], 12.0, 0.07, EASE, start=0.5)
shots.append(s)

D2 = load(os.path.join(STYLE, "SF_D2.json"))
s = from_still(D2, "D2_TRACK", "Editorial motion", "D2 Macro row: lateral track", 12.0,
               "The macro row, tracking: the camera moves 3.5 cm sideways over twelve seconds, centred on the approved "
               "still at the midpoint. Focus travels with the camera (the same distance throughout), so the focal plane "
               "never hunts: the fluted knobs pass through it one after another, the keys soft in the foreground.")
s["camera"] = lateral(D2["camera"], 12.0, 0.035, EASE, start=0.5)
shots.append(s)

# --------------------------------------------------------------------- H01: lightweight preview (still first)
H = load(os.path.join(HERO, "H01_B.json"))
c = H["camera"]
d = [p - t for p, t in zip(c["position"], c["target"])]
s = from_still(H, "H01_B_DOLLY", "H01 Front-Facing Monument", "Ivory Gallery: dolly back (preview)", 10.0,
               "The hero, breathing: the camera eases straight back by 4 % (no height change, no yaw, no roll) and "
               "settles on the approved still, which is also the poster frame and the reduced-motion fallback. The "
               "cove light drifts 6 cm to the right, so the light on the ivory shifts almost imperceptibly.")
s["camera"] = dict(c, position=[[0.0, add(c["target"], [d[0], d[1] / 1.04, d[2] / 1.04]), SETTLE], [10.0, c["position"]]])
s["lights"] = {"Cove light": {"translate": [[0.0, [-0.06, 0.0, 0.0], "linear"], [10.0, [0.0, 0.0, 0.0]]]}}
s["typography"] = {"hero_layout": "H01_B"}
shots.append(s)

for s in shots:
    json.dump(s, open(os.path.join(OUT, s["id"] + ".json"), "w"), indent=1)
print(len(shots), "motion specs ->", os.path.normpath(OUT))
