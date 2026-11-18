"""Prepares a captured performance for performance_anim.py (system Python + Pillow).

  python3 product-renders/blender/scripts/performance_prep.py <performance dir> <out dir>

Crops every captured face frame (the editor without knob caps) exactly as prep_textures.py crops
the master's osp_face.png (same width, the master's aspect, from the top), numbered from 1 for
Blender's image sequence.
"""
import glob
import os
import sys

from PIL import Image

perf, out = sys.argv[1:3]
here = os.path.dirname(os.path.abspath(__file__))
master = Image.open(os.path.join(here, "..", "textures_current", "osp_face.png"))
seq = os.path.join(out, "face_seq")
os.makedirs(seq, exist_ok=True)
files = sorted(glob.glob(os.path.join(perf, "face", "*.png")))
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB")
    h = round(im.width * master.height / master.width)
    im.crop((0, 0, im.width, h)).save(os.path.join(seq, f"{i + 1:04d}.png"))
print(len(files), "face frames ->", seq, f"(master {master.size}, aspect kept)")
