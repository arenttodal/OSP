#!/usr/bin/env python3
"""Writes the style-frame sheets (rationale, camera, lights, stage) from the specs, so the document
can never disagree with what was rendered.

  python3 style_report.py > ../../../documentation/art_direction_r2_frames.md
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ids = sorted(f[:-5] for f in os.listdir(os.path.join(HERE, "specs")) if f.endswith(".json"))


def f3(v):
    return "[" + ", ".join(f"{x:g}" for x in v) + "]" if isinstance(v, list) else f"{v:g}"


print("# Art Direction Gate R2: style-frame sheets\n")
print("Generated from `blender/cine/style/specs/*.json` (written by `make_style.py`) by `style_report.py`.\n")
print("**Coordinates:** metres in the product frame, measured from the instrument's centre at rest:")
print("- x runs along the keys, to the right;")
print("- y runs toward the back;")
print("- z is up.\n")
print("The Monument family stands the instrument on the walnut floor; in those frames the top surface is near z = −0.086.\n")
print("**Renders:** 1920 × 1080, 128 samples, noise threshold 0.01, 16-bit PNG.\n")
for n, i in enumerate(ids, 1):
    s = json.load(open(os.path.join(HERE, "specs", i + ".json")))
    c, st = s["camera"], s.get("stage", {})
    d = math.dist(c["position"], c["target"])
    print(f"## {n:02d} · {i}: {s['family']}, {s['variant']}\n")
    print(f"> {s['rationale']}\n")
    print("| | |\n|---|---|")
    print(f"| Base | {s['setup']} (lights and materials of shot {s['base_shot']}); 16:9 |")
    print(f"| Lens | {c['lens']} mm, f/{c.get('fstop', '–')}; visible width at the target {36 * d / c['lens']:.2f} m (instrument 0.40 m) |")
    print(f"| Camera | position {f3(c['position'])}, target {f3(c['target'])}" + (f", focus {f3(c['focus'])}" if "focus" in c else "")
          + (f", shift {f3(c['shift'])}" if "shift" in c else "") + " |")
    pr = s.get("product")
    if pr:
        print("| Product | " + "; ".join(f"{k} {f3(v)}" for k, v in pr.items()) + " |")
    if st.get("cyc"):
        cy = st["cyc"]
        print(f"| Stage | seamless cyclorama: colour {f3(cy['rgb'])}, cove radius {cy['radius']} m, wall {cy['wall']} m behind (no horizon line) |")
    else:
        print("| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |")
    if st.get("remove_lights"):
        print(f"| Setup lights off | {', '.join(st['remove_lights'])} |")
    for k, v in (s.get("lights") or {}).items():
        print(f"| Setup light {k} | " + ", ".join(f"{a} × {f3(b)}" for a, b in v.items()) + " |")
    for L in st.get("add_lights", []):
        print(f"| Light: {L['name']} | {L['size'][0]:g} × {L['size'][1]:g} m, {L['power']:g} W, at {f3(L['position'])} aimed at {f3(L['target'])}"
              + (f", spread {L['spread']}°" if L.get("spread", 180) != 180 else "") + (f", lights the {L['link']} only" if L.get("link") else "") + " |")
    for fl in st.get("flags", []):
        where = "where the display glass reflects the camera" if fl.get("mirror") else f"at {f3(fl['position'])}"
        print(f"| Flag: {fl['name']} | black, {fl['size'][0]:g} × {fl['size'][1]:g} m, {where}; "
              + ("absorbs bounce (negative fill)" if fl.get("mode") == "negative" else "seen only in reflections") + " |")
    for cd in s.get("cards", []):
        print(f"| Card: {cd['name']} | {cd['size'][0]:g} × {cd['size'][1]:g} m, strength {cd['strength']:g}, its highlight placed on {f3(cd['mirror']['aim'])}; the instrument only |")
    print(f"| Exposure / world / displays | {s.get('exposure', 0):+g} EV / × {s.get('world', 1):g} / × {s.get('emission', 1):g} |")
    print()
