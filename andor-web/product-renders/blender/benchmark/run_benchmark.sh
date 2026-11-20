#!/bin/bash
# ANDOR final-quality benchmark: R04 Window Light (16 s, 384 frames) on the frozen 0.9.0-beta.1 interface.
# usage (from the bundle root): ./run_benchmark.sh <DEVICE> [<blender binary>]
#   DEVICE: OPTIX or CUDA (NVIDIA), HIP (AMD), METAL (Apple Silicon), ONEAPI (Intel Arc), CPU
# Writes bench_<DEVICE>/: parity/ (2 frames, 1920 px, for compare_reference.py), timing/ (6 frames, 3840 px,
# three consecutive for the flicker check) and the per-frame seconds in each pass's *_meta.json.
# No account, upload or external spend is made by this script.
set -e
DEV=${1:?device}; BL=${2:-blender}
cd "$(dirname "$0")/andor-web"
SPEC=product-renders/blender/cine/shots/R04_WIN16.json; RIG=product-renders/blender/cine/cine_rig.py
OUT=../bench_$DEV; mkdir -p "$OUT"
run() { "$BL" -b --python "$RIG" -- --spec "$SPEC" --device "$DEV" --final "$@"; }
# parity: production settings at 1920 px, the frames the CPU reference holds
for f in 25 217; do run --out "$OUT/parity" --width 1920 --samples 96 --noise 0.01 --frames $f $f; cp "$OUT/parity/R04_WIN16_beauty_meta.json" "$OUT/parity/meta_$f.json"; done
# timing: production settings at the final 3840 px
for f in 25 121 217 218 219 313; do run --out "$OUT/timing" --width 3840 --samples 96 --noise 0.01 --frames $f $f; cp "$OUT/timing/R04_WIN16_beauty_meta.json" "$OUT/timing/meta_$f.json"; done
python3 - "$OUT" <<'PY'
import glob, json, os, sys
out = sys.argv[1]; rows = []
for m in sorted(glob.glob(os.path.join(out, "*", "meta_*.json"))):
    j = json.load(open(m))
    for fr in j["frames"]:
        if "render_s" in fr: rows.append({"pass": os.path.basename(os.path.dirname(m)), "res": j["res"], "frame": int(os.path.basename(m)[5:-5]), "s": fr["render_s"], "devices": j["devices"]})
json.dump(rows, open(os.path.join(out, "timings.json"), "w"), indent=1)
t = [r["s"] for r in rows if r["res"][0] == 3840]
print("devices", rows[0]["devices"] if rows else "?"); print("3840 px seconds per frame:", t)
if t: print("mean %.1f s/frame -> R04 (384 frames): %.1f h" % (sum(t) / len(t), sum(t) / len(t) * 384 / 3600))
PY
