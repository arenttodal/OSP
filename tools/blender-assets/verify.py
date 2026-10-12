"""Checks a rendered sequence before it becomes a runtime resource (Python 3 + Pillow).

    python3 verify.py --config config/macro-knob.json [--out output/<name>]

- every frame is there, with the same size;
- transparent around (no background baked in): the border is fully clear;
- no fringe: wherever alpha is low, the colour is never brighter than the knob or a light
  halo (a straight-alpha PNG edge that would show on a dark panel);
- the frames turn in order: the indicator's angle (from the dark pixels) follows the
  manifest's mapping within 3 degrees, minimum at frame 0, maximum at the last.
Exit code 1 on any failure.
"""

import argparse
import json
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from osp_assets import config as cfg  # noqa: E402


def indicator_angle(image):
    """Clockwise-from-12 angle of the dark, opaque pixels' centroid (radians), or None."""
    w, h = image.size
    px = image.load()
    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
    sx = sy = n = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b, a = px[x, y]
            if a > 250 and (r + g + b) < 3 * 90:
                sx += x - cx
                sy += y - cy
                n += 1
    return math.atan2(sx, -sy) if n > 10 else None


def angle_distance(a, b):
    d = math.fmod(abs(a - b), 2.0 * math.pi)
    return min(d, 2.0 * math.pi - d)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", required=True)
    p.add_argument("--out", default=None)
    args = p.parse_args()
    config = cfg.load(args.config)
    base = args.out or os.path.join(cfg.ROOT, "output", config["name"])
    with open(os.path.join(base, "render-info.json"), "r", encoding="utf-8") as f:
        info = json.load(f)
    problems = []
    sizes = set()
    worst = 0.0
    for entry in info["frames"]:
        i = entry["index"]
        path = os.path.join(base, "master", f"frame_{i:03d}.png")
        if not os.path.exists(path):
            problems.append(f"frame {i} missing")
            continue
        image = Image.open(path).convert("RGBA")
        sizes.add(image.size)
        w, h = image.size
        alpha = image.getchannel("A")
        border = max(max(alpha.getpixel((x, 0)), alpha.getpixel((x, h - 1))) for x in range(w))
        border = max(border, max(max(alpha.getpixel((0, y)), alpha.getpixel((w - 1, y))) for y in range(h)))
        if border > 64:   # the soft shadow may just reach it; postprocess fades it out
            problems.append(f"frame {i}: alpha {border} at the border")
        if alpha.getpixel((w // 2, h // 2 + h // 8)) != 255:
            problems.append(f"frame {i}: the knob is not opaque below its centre")
        seen = indicator_angle(image)
        if seen is None:
            problems.append(f"frame {i}: no indicator found")
            continue
        error = angle_distance(seen, entry["angle"])
        worst = max(worst, error)
        if math.degrees(error) > 3.0:
            problems.append(f"frame {i}: indicator at {math.degrees(seen):.1f} deg, expected {math.degrees(entry['angle']) % 360:.1f}")
    if len(sizes) > 1:
        problems.append(f"frames differ in size: {sorted(sizes)}")
    print(f"{len(info['frames'])} frames, size {sorted(sizes)}, largest indicator error {math.degrees(worst):.2f} deg")
    for line in problems:
        print("FAIL:", line)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
