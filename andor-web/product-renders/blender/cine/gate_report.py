#!/usr/bin/env python3
"""Writes the creative-gate shot sheets (narrative + camera, object and light parameters) from the
shot specs, so the document can never disagree with what was rendered.

  python3 gate_report.py [--ids R01_A R01_B ...] > ../../documentation/cinematic_gate_shots.md
"""
import argparse
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--ids", nargs="*")
a = ap.parse_args()
ids = a.ids or sorted(f[:-5] for f in os.listdir(os.path.join(HERE, "shots")) if f.endswith(".json"))


def fmt(v):
    if isinstance(v, list):
        return "[" + ", ".join(f"{x:g}" if isinstance(x, (int, float)) else str(x) for x in v) + "]"
    return f"{v:g}" if isinstance(v, (int, float)) else str(v)


def ease(e):
    if isinstance(e, list):
        return "bezier " + fmt(e)
    return e


def keys(ch):
    if not isinstance(ch, list) or not ch or not isinstance(ch[0], list):
        return fmt(ch) + " (constant)"
    out = []
    for i, k in enumerate(ch):
        s = f"{k[0]:g} s: {fmt(k[1])}"
        if len(k) > 2 and i < len(ch) - 1:
            s += f" ({ease(k[2])})"
        out.append(s)
    return " → ".join(out)


def dist(p, t):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(p, t)))


print("# Creative gate R01–R03: shot sheets\n")
print("Generated from `blender/cine/shots/*.json` by `gate_report.py`.\n")
print("**Coordinates:** metres in the product frame, from the instrument's centre at rest:")
print("- x along the keys, to the right;")
print("- y toward the back;")
print("- z up.\n")
print("**Visible width:** at distance d with focal length L, the visible width is 36·d/L (36 mm sensor).\n")
print("**Times** are seconds into the move. Every shot has a 1 s handle at each end that holds the end pose.\n")
for i in ids:
    s = json.load(open(os.path.join(HERE, "shots", i + ".json")))
    c = s["camera"]
    print(f"## {s['id']}: {s['family']}, {s['variant']}\n")
    print(f"> {s['narrative']}\n")
    print("| | |\n|---|---|")
    print(f"| Setup | {s['setup']} (base light rig of shot {s['base_shot']}); {s['aspect'][0]}:{s['aspect'][1]}, 24 fps, {s['seconds']:g} s + {s.get('handles', 1):g} s handles |")
    print(f"| Lens | {keys(c['lens'])} mm, f/{fmt(c.get('fstop'))} |")
    print(f"| Camera position | {keys(c['position'])} |")
    print(f"| Camera target | {keys(c['target'])} |")
    if "focus" in c:
        print(f"| Focus point | {keys(c['focus'])} |")
    p0 = c["position"][0][1] if isinstance(c["position"][0], list) else c["position"]
    p1 = c["position"][-1][1] if isinstance(c["position"][0], list) else c["position"]
    t0 = c["target"][0][1] if isinstance(c["target"][0], list) else c["target"]
    t1 = c["target"][-1][1] if isinstance(c["target"][0], list) else c["target"]
    L = c["lens"] if isinstance(c["lens"], (int, float)) else c["lens"][0][1]
    print(f"| Visible width (at target) | open {36 * dist(p0, t0) / L:.2f} m → close {36 * dist(p1, t1) / L:.2f} m (instrument: 0.40 m) |")
    pr = s.get("product")
    if pr:
        for k in ("tilt", "turn", "translate"):
            if k in pr:
                print(f"| Product {k} | {keys(pr[k])}{' °' if k != 'translate' else ''} |")
    else:
        print("| Product | stationary |")
    for name, lc in s.get("lights", {}).items():
        parts = [f"{k} {keys(v)}" for k, v in lc.items()]
        print(f"| Light: {name} | {'; '.join(parts)} |")
    for cd in s.get("cards", []):
        print(f"| Card: {cd['name']} | {cd['size'][0]:g} × {cd['size'][1]:g} m; position {keys(cd['position'])}; aim {keys(cd['target'])}; "
              f"strength {keys(cd.get('strength', 1))}; {'lights diffuse too' if cd.get('diffuse') else 'reflection only'} |")
    for k in ("exposure", "world", "emission"):
        if k in s:
            print(f"| {k.capitalize()} | {keys(s[k])} |")
    if s.get("typography"):
        print(f"| Typography | {s['typography']['wordmark']} behind the product (composited through the matte) |")
    if s.get("notes"):
        print(f"| Notes | {s['notes']} |")
    print()
