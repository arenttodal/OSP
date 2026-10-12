"""Renders the ceramic skin's pieces (pieces.py) with the design lab's studio and materials.

    <bpy python> render.py [--only GLOB[,GLOB...]] [--force] [--samples N]
    blender -b --factory-startup -P render.py -- [options]

The look comes from the design lab's chosen direction (DIRECTION in pieces.py): its
materials, the approved knob's light rig and the lab's calibrated exposure
(design-lab/blender/configs). Nothing here restates a colour or a light. Output goes to
tools/blender-assets/output/ceramic/raw/ (git-ignored); pack.py makes the runtime pack.
"""

import argparse
import fnmatch
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LAB_BLENDER = os.path.join(REPO, "design-lab", "blender")
sys.path.insert(0, HERE)
sys.path.insert(0, LAB_BLENDER)

import bpy  # noqa: E402

from lab import forms, geometry, studio  # noqa: E402
from lab.materials import material  # noqa: E402
import pieces as P  # noqa: E402

OUT = os.path.join(REPO, "tools", "blender-assets", "output", "ceramic", "raw")


def config(name):
    with open(os.path.join(LAB_BLENDER, "configs", name), "r", encoding="utf-8") as f:
        return json.load(f)


CTRL = config("controls.json")
SPEC = config("directions.json")["directions"][P.DIRECTION]
EXPOSURE = config("calibration.json")["exposure"]
HOVER_TINT = ["#ffffff", 0.16]


def ref(role, state=None):
    """The material reference of a role in the chosen direction (a fresh dict)."""
    enc = CTRL["enclosures"][SPEC["enclosure"]]
    if role == "housing":
        r = enc["housing"]
    elif role == "card":
        r = SPEC.get("cardMaterial", enc["card"])
    elif role == "panel":
        r = CTRL["panels"][SPEC["panel"]]["material"]
    elif role in ("key", "key-arp"):
        r = CTRL["buttons"][SPEC["buttons"]]["material"]
    elif role == "rail":
        r = CTRL["tracks"][SPEC["track"]]["material"]
    elif role == "fader":
        r = CTRL["faders"][SPEC["fader"]]["material"]
    elif role == "teak":
        r = CTRL["roles"]["teak"]
    elif isinstance(role, dict) and "badge" in role:
        r = {"preset": "M2-satin-ceramic-ivory", "baseColor": role["badge"], "roughness": 0.4, "coat": 0.12}
    else:
        r = CTRL["roles"][role]
    r = {"preset": r} if isinstance(r, str) else dict(r)
    if state == "hover":
        r["tint"] = HOVER_TINT
    elif state == "active":
        r["tint"] = CTRL["buttons"][SPEC["buttons"]]["activeTint"]
    elif state == "drag":
        r["tint"] = HOVER_TINT
    return r


BASE_TOP = {"housing": P.HOUSING_TOP, "card": P.CARD_TOP, "panel": P.PANEL_TOP,
            "key-arp": P.HOUSING_TOP + 0.8 * P.EMPHASIS["arp"]}


def expand(rect, d):
    return (rect[0] - d, rect[1] - d, rect[2] + 2 * d, rect[3] + 2 * d)


def catcher(rect, base):
    obj = geometry.plane("catcher", expand(rect, 40), material(ref(base)), BASE_TOP[base])
    obj.is_shadow_catcher = True
    return obj


def housing_scene():
    trim = CTRL["trim"][SPEC["trim"]]
    hg = CTRL["geometry"]["housing"]
    geometry.plane("backdrop", (-200, -200, 1848, 1486), material(CTRL["roles"]["backdrop"]))
    forms.slab("housing", P.HOUSING, trim["housingRadius"], 0.0, hg["height"], hg["bevel"], material(ref("housing")))
    for side in ("left", "right"):
        forms.slab("trim", tuple(trim[side]), trim["radius"], 0.0, trim["height"], trim["bevel"], material(ref("teak")))


def slab_scene(p):
    catcher(p["rect"], p["base"])
    forms.slab(p["name"], p["rect"], p["radius"], BASE_TOP[p["base"]], p["height"], p["bevel"], material(ref(p["material"], p.get("state"))))


def fader_scene(p):
    """C1 on its base (as the lab's plan_fader): grip grooves across the travel; hover and
    drag lift the sheen, drag also lifts the cap a little."""
    catcher(p["rect"], p["base"])
    c = CTRL["faders"][SPEC["fader"]]
    x, y, w, h = p["rect"]
    cx, cy = x + 0.5 * w, y + 0.5 * h
    lift = 0.25 if p["state"] == "drag" else 0.0
    pockets = []
    for k in range(c.get("grip", 0)):
        gx = cx + (k - 0.5 * (c["grip"] - 1)) * 3.4
        pockets.append({"rect": (gx - 0.55, cy - 0.32 * h, 1.1, 0.64 * h), "radius": 0.55, "depth": 0.16, "lip": 0.05})
    forms.slab("fader-cap", p["rect"], c["radius"], P.HOUSING_TOP + lift, c["height"], c["bevel"],
               material(ref("fader", None if p["state"] == "normal" else p["state"])), pockets=pockets)


def lens_scene(p):
    """A lens LED (as the lab's plan_leds): lit, it glows in its colour; unlit, a dim tinted
    lens; the ARP light off is graphite."""
    cx, cy = p["centre"]
    d = p["diameter"]
    rect = (cx - 0.5 * d, cy - 0.5 * d, d, d)
    catcher(rect, p["base"])
    col = p["colour"]
    if p.get("graphite"):
        lens = {"preset": "M5-graphite-powder-coat", "baseColor": col, "roughness": 0.3, "coat": 0.5}
    else:
        lens = {"preset": "M2-satin-ceramic-ivory", "baseColor": col, "roughness": 0.22, "coat": 0.6,
                "emission": [col, 1.4 if p["lit"] else 0.12]}
        if not p["lit"]:
            lens["tint"] = ["#ece5d8", 0.3]
    forms.dome("led", (cx, cy), d, BASE_TOP[p["base"]], p["height"], material(lens))


def pocket_scene(p, cut):
    parent = p["parent"]
    if parent == "housing":
        hg = CTRL["geometry"]["housing"]
        trim = CTRL["trim"][SPEC["trim"]]
        z0, height, bevel, radius = 0.0, hg["height"], hg["bevel"], trim["housingRadius"]
    elif parent == "card":
        g = CTRL["geometry"]["card"]
        z0, height, bevel, radius = P.HOUSING_TOP, g["height"], g["bevel"], 16
    else:
        panel = CTRL["panels"][SPEC["panel"]]
        z0, height, bevel, radius = P.HOUSING_TOP, panel["height"], panel["bevel"], 13
    pockets, extra = [], ()
    if cut:
        pockets = [{"rect": p["rect"], "radius": p["radius"], "depth": p["depth"], "lip": p["lip"], "floor": 1}]
        extra = (material(CTRL["roles"][p["floor"]]),)
    forms.slab("parent", p["parentRect"], radius, z0, height, bevel, material(ref(parent)), pockets=pockets, extra_materials=extra)


def render(p, path, build, transparent=True, samples=None):
    scene = studio.reset()
    studio.rig(scene)
    build()
    studio.camera(scene, expand(p["rect"], p["margin"]), p["scale"])
    studio.settings(scene, samples=samples or p["samples"], exposure=EXPOSURE, transparent=transparent)
    studio.render(scene, path)
    print("rendered", os.path.relpath(path, REPO), flush=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="*")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--samples", type=int)
    a = ap.parse_args(argv)
    globs = a.only.split(",")
    for p in P.pieces():
        if not any(fnmatch.fnmatch(p["name"], g) for g in globs):
            continue
        if p["kind"] == "pocket":
            for cut, suffix in ((True, "with"), (False, "without")):
                path = os.path.join(OUT, f"{p['name']}.{suffix}.png")
                if a.force or not os.path.exists(path):
                    render(p, path, lambda cut=cut: pocket_scene(p, cut), transparent=False, samples=a.samples)
            continue
        path = os.path.join(OUT, p["name"] + ".png")
        if os.path.exists(path) and not a.force:
            continue
        if p["kind"] == "opaque":
            render(p, path, housing_scene, transparent=False, samples=a.samples)
        elif p["kind"] == "slab":
            render(p, path, lambda: slab_scene(p), samples=a.samples)
        elif p["kind"] == "fader":
            render(p, path, lambda: fader_scene(p), samples=a.samples)
        elif p["kind"] == "lens":
            render(p, path, lambda: lens_scene(p), samples=a.samples)


if __name__ == "__main__":
    main()
