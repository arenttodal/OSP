"""Design lab renders (headless Blender: `python generate.py ...` with the bpy module, or
`blender -b -P generate.py -- ...`).

    python generate.py calibrate
    python generate.py swatches [--quality preview|final]
    python generate.py scene <name> [--quality ...]     # scenes defined in scenes.py

Every render uses the same rig, camera conventions and exposure (calibration.json), so the
differences between candidates come from their geometry and materials only.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402

from lab import geometry, studio  # noqa: E402
from lab.materials import by_name, presets  # noqa: E402

LAB = os.path.dirname(HERE)
CALIBRATION = os.path.join(LAB, "blender", "configs", "calibration.json")
QUALITY = {"draft": {"samples": 8, "scale": 1.0}, "preview": {"samples": 24, "scale": 1.0}, "final": {"samples": 48, "scale": 2.0}}
TARGET = "#efe8dd"   # M1 on a flat panel = the GUI's panel ivory (between panelTop and cardTop)


def exposure():
    if os.path.exists(CALIBRATION):
        with open(CALIBRATION, "r", encoding="utf-8") as f:
            return json.load(f)["exposure"]
    return 0.0


def calibrate():
    """The exposure that renders a flat M1 surface at TARGET (iterative, deterministic)."""
    import math
    from lab.materials import srgb_to_linear
    target = sum(w * c for w, c in zip((0.2126, 0.7152, 0.0722), srgb_to_linear(TARGET)))
    ev = 0.0
    probe = os.path.join(LAB, "work", "calibration-probe.png")
    for _ in range(10):
        scene = studio.reset()
        studio.rig(scene)
        geometry.plane("plate", (310, 740, 100, 100), by_name("M1-warm-matte-ivory"))
        studio.camera(scene, (310, 740, 100, 100), 0.64)
        studio.settings(scene, samples=32, exposure=ev)
        studio.render(scene, probe)
        img = bpy.data.images.load(probe, check_existing=False)
        px = img.pixels[:]
        w, h = img.size
        bpy.data.images.remove(img)
        total = 0.0
        n = 0
        for y in range(h // 4, 3 * h // 4, 2):
            for x in range(w // 4, 3 * w // 4, 2):
                i = 4 * (y * w + x)
                lin = [(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4) for v in px[i:i + 3]]
                total += 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
                n += 1
        measured = total / n
        if measured >= 0.985:
            ev -= 1.0
            continue
        step = math.log2(target / measured)
        ev += step
        if abs(step) < 0.01:
            break
    os.makedirs(os.path.dirname(CALIBRATION), exist_ok=True)
    with open(CALIBRATION, "w", encoding="utf-8") as f:
        json.dump({"exposure": round(ev, 4), "target": TARGET, "measuredLuminance": measured, "material": "M1-warm-matte-ivory"}, f, indent=2)
    print(f"calibrated: exposure {ev:+.3f} EV")


def swatches(quality):
    """The six materials on rounded tiles (220 x 220 px each), same light and exposure."""
    q = QUALITY[quality]
    names = [n for n in presets() if n.startswith("M")]
    out = os.path.join(LAB, "renders", "materials")
    for name in names:
        scene = studio.reset()
        studio.rig(scene)
        rect = (240, 690, 240, 240)   # in the GUI's space (under the macros): lit as there
        studio.shadow_catcher(scene, (40, 490, 640, 640))
        geometry.raised(name, (250, 700, 220, 220), 22, 3.0, 1.0, by_name(name))
        studio.camera(scene, rect, q["scale"] * 2.0)
        studio.settings(scene, samples=q["samples"], exposure=exposure())
        studio.render(scene, os.path.join(out, f"{name}.png"))
        print("swatch", name, flush=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    p = argparse.ArgumentParser()
    p.add_argument("what")
    p.add_argument("name", nargs="?")
    p.add_argument("--quality", default="preview", choices=list(QUALITY))
    a = p.parse_args(argv)
    if a.what == "calibrate":
        calibrate()
    elif a.what == "swatches":
        swatches(a.quality)
    elif a.what == "scene":
        import scenes
        scenes.run(a.name, QUALITY[a.quality], exposure())
    else:
        raise SystemExit(f"unknown: {a.what}")


if __name__ == "__main__":
    main()
