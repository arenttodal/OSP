#!/usr/bin/env python3
"""Colour / material parity of a render against the CPU reference (same frames, same settings).

  python3 compare_reference.py <reference frames dir> <test frames dir>

Per frame: mean absolute difference, 99.9th percentile, and the mean colour shift per channel
(test - reference, in levels of 255). Noise differs between devices (expected); a colour shift
does not (it would mean a different view transform, OCIO config or Blender version)."""
import glob, os, sys
import numpy as np
from PIL import Image
ref_dir, test_dir = sys.argv[1:3]
worst = 0.0
for r in sorted(glob.glob(os.path.join(ref_dir, "*.png"))):
    t = os.path.join(test_dir, os.path.basename(r))
    if not os.path.exists(t):
        print("missing", os.path.basename(r)); continue
    a = np.asarray(Image.open(r).convert("RGB"), float)
    b = np.asarray(Image.open(t).convert("RGB"), float)
    d = np.abs(a - b)
    shift = (b - a).reshape(-1, 3).mean(0)
    worst = max(worst, float(np.abs(shift).max()))
    print(f"{os.path.basename(r)}: mean {d.mean():.2f}, p99.9 {np.percentile(d, 99.9):.1f}, colour shift R {shift[0]:+.2f} G {shift[1]:+.2f} B {shift[2]:+.2f}")
print("PARITY", "OK" if worst < 0.5 else "CHECK (colour shift %.2f levels)" % worst)
