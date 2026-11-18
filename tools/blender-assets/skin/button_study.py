"""Phase 4 exploratory study (not integrated): ceramic buttons and the touch strip's layers.

    <bpy python> tools/blender-assets/skin/button_study.py [--samples N]

Same studio, materials and calibrated exposure as the ceramic skin (render.py); fixed
orthographic cameras; transparent backgrounds; the parts at their real GUI sizes. Labels,
icons, values and the touch strip's lit indication stay live vector in the plugin, so only
the physical backgrounds are rendered here.

Variants of the key (A1 roles: icon button, push button, modifier, ARP), every state:
  K0  the current skin key (pieces.py): height 0.8 x emphasis, bevel min(0.55, 0.9 h), 8 segments
  K1  a rounder, more photographic bevel (0.95 h, 12 segments), a restrained satin coat on the
      top, and a deeper press (0.30 x emphasis) so pressed / active read clearly
  K2  K1's press and coat with a crisper edge (bevel 0.35 mm, 6 segments): a fine highlight line
      instead of a soft shoulder
Touch strip (PITCH / MOD): the ivory bezel (raised ring) and the stationary charcoal sensor,
as separate transparent layers at the strip's size.
Output: tools/blender-assets/output/ceramic-study/<variant>/<name>.png (git-ignored).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render as R  # noqa: E402  (the skin renderer: studio, materials, exposure)
import pieces as P  # noqa: E402

OUT = os.path.join(R.REPO, "tools", "blender-assets", "output", "ceramic-study")

KEYS = [   # role, rect (design px), radius, base
    ("icon", (250, 143, 38, 33), 6, "card"),          # EQ-sized icon button
    ("push", (700, 400, 84, 36), 7, "card"),          # a standard push button
    ("modifier", (51, 586, 98, 45), 7, "card"),
    ("arp", (1308, 913, 105, 64), 10, "housing"),
]
EMPH = {"icon": P.EMPHASIS["eq"], "push": P.EMPHASIS["generic"], "modifier": P.EMPHASIS["modifier"], "arp": P.EMPHASIS["arp"]}


def key_params(variant, role, state):
    e = EMPH[role]
    pressed = state in ("pressed", "active")
    if variant == "K0":
        h = (0.42 if pressed else 0.8) * e
        return h, min(0.55, 0.9 * h), 8, 0.0
    h = (0.30 if pressed else 0.8) * e
    if variant == "K1":
        return h, 0.95 * h, 12, 0.18
    return h, min(0.35, 0.9 * h), 6, 0.18     # K2


def key_scene(rect, radius, base, variant, role, state):
    h, bevel, segments, coat = key_params(variant, role, state)
    R.catcher(rect, base)
    mat = R.ref("key", state)
    if coat:
        from lab.materials import resolve
        mat["coat"] = resolve(mat)[1].get("coat", 0.0) + coat   # on top of the preset's own coat
        mat["coatRoughness"] = 0.28
    R.forms.slab(f"key-{role}", rect, radius, R.BASE_TOP[base], h, bevel, R.material(mat), segments=segments)


STRIP = (52, 925, 34, 86)   # PITCH's sensor opening (design px, at the keyboard's left)


def strip_bezel():
    x, y, w, h = STRIP
    g = 3.0
    outer = (x - g, y - g, w + 2 * g, h + 2 * g)
    R.catcher(outer, "housing")
    R.forms.slab("bezel", outer, 8, R.BASE_TOP["housing"], 0.4, 0.3, R.material(R.ref("key")),
                 pockets=[{"rect": STRIP, "radius": 6, "depth": 0.39, "lip": 0.12, "floor": None}])


def strip_sensor():
    R.catcher(STRIP, "housing")
    sensor = {"preset": "M5-graphite-powder-coat", "baseColor": "#131617", "roughness": 0.42, "coat": 0.05, "coatRoughness": 0.32,
              "specular": 0.14}   # Phase 5: calibrated to the editor's own MOD sensor (sRGB ~50/53/53, luminance 52)
    R.forms.slab("sensor", STRIP, 6, R.BASE_TOP["housing"], 0.28, 0.12, R.material(sensor), segments=8)   # inside the bezel, its top 0.12 mm below the bezel's


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=64)
    a = ap.parse_args(argv)
    for variant in ("K0", "K1", "K2"):
        for role, rect, radius, base in KEYS:
            for state in P.KEY_STATES:
                path = os.path.join(OUT, variant, f"key-{role}-{state}.png")
                if os.path.exists(path):
                    continue
                os.makedirs(os.path.dirname(path), exist_ok=True)
                piece = {"rect": rect, "margin": 10, "scale": 4, "samples": a.samples}
                R.render(piece, path, lambda: key_scene(rect, radius, base, variant, role, state), samples=a.samples)
    for name, build in (("strip-bezel", strip_bezel), ("strip-sensor", strip_sensor)):
        path = os.path.join(OUT, "strip", name + ".png")
        if not os.path.exists(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            x, y, w, h = STRIP
            R.render({"rect": (x - 3, y - 3, w + 6, h + 6), "margin": 10, "scale": 4, "samples": a.samples}, path, build, samples=a.samples)


if __name__ == "__main__":
    main()
