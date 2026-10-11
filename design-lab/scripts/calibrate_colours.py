"""Match the lab's ivory surfaces to the production GUI (Direction 01 = the current look).

Renders Direction 01's chassis (preview), compares surface regions that carry no drawing in
the production screenshot (housing, cards, macro panel, backdrop) and scales every ivory base
colour in controls.json / directions.json by the mean per-channel ratio (linear light). The
lab's light is neutral; the bias it corrects is the scene's own inter-reflection and the
softbox gradient. Metals and graphite are left alone. Run until the residual is < 1 %.

    python scripts/calibrate_colours.py <blender-python> [iterations]
"""
import json, os, subprocess, sys
import numpy as np
from PIL import Image

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(LAB, "blender", "configs")
REGIONS = {"housing-upper": (40, 108, 440, 14), "housing-lower": (660, 1030, 700, 30), "card-a": (60, 454, 380, 8),
           "card-b": (520, 455, 400, 8), "panel-left": (40, 735, 16, 120), "panel-env": (960, 815, 8, 60), "backdrop": (2, 300, 18, 500)}
SKIP = ("M4", "M5", "dark-slot", "graphite-key", "knob-satin-ivory")


def lin(c):
    c = np.asarray(c, float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(l):
    l = np.clip(l, 0, 1)
    return np.where(l <= 0.0031308, 12.92 * l, 1.055 * l ** (1 / 2.4) - 0.055) * 255.0


def mean(img, r):
    x, y, w, h = r
    return lin(img[y:y + h, x:x + w].reshape(-1, 3)).mean(0)


def walk(node, factor):
    if isinstance(node, dict):
        if "baseColor" in node and not any(str(node.get("preset", "")).startswith(s) for s in SKIP):
            h = node["baseColor"].lstrip("#")
            c = lin([int(h[i:i + 2], 16) for i in (0, 2, 4)]) * factor
            node["baseColor"] = "#" + "".join(f"{int(round(v)):02x}" for v in srgb(c))
        for v in node.values():
            walk(v, factor)
    elif isinstance(node, list):
        for v in node:
            walk(v, factor)


def main():
    bpy_python = sys.argv[1]
    iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    ref = np.asarray(Image.open(os.path.join(LAB, "references", "current-gui", "export1x", "full@1x.png")).convert("RGB"))
    for it in range(iterations):
        subprocess.run([bpy_python, "generate.py", "scene", "direction:01-original-refined"], cwd=os.path.join(LAB, "blender"),
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        out = np.asarray(Image.open(os.path.join(LAB, "renders", "chassis", "01-original-refined@1x.png")).convert("RGB"))
        ratios = []
        for k, r in REGIONS.items():
            a, b = mean(out, r), mean(ref, r)
            ratios.append(b / a)
            print(f"  {k:14s} render {srgb(a).round()} production {srgb(b).round()}")
        factor = np.exp(np.log(np.array(ratios)).mean(0))
        err = float(np.abs(factor - 1).max())
        print(f"iteration {it}: factor {factor.round(4)} (max {err:.3%})", flush=True)
        if err < 0.01:
            break
        for name in ("controls.json", "directions.json"):
            path = os.path.join(CFG, name)
            with open(path, "r", encoding="utf-8") as f:
                d = json.load(f)
            walk(d, factor)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(d, f, indent=4)
                f.write("\n")


if __name__ == "__main__":
    main()
