#!/bin/bash
# Final-quality review renders (fq job; run on the pod by pod_runner.py, JOBS set at the top) at 1920 x 1080 (96 samples, noise 0.01, GPU denoise, frozen interface).
# Each line of JOBS: <shot id> <first body frame> <last body frame> <pass>. Output: one H.264 master per shot
# (exact frame count), five keyframes, and for R01_PORTRAIT_R4 the 8-bit frames and matte for the typography pass.
set -e
DEV=${1:?device}; BL=${2:-blender}
cd "$(dirname "$0")/andor-web"
RIG=product-renders/blender/cine/cine_rig.py; SH=product-renders/blender/cine/shots
OUT=../bench_$DEV; RAW=../raw_$DEV; mkdir -p "$OUT" "$RAW"
(DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ffmpeg zip > /dev/null 2>&1 || true) &
while read -r id f0 f1 pass; do
  [ -z "$id" ] && continue
  "$BL" -b --python "$RIG" -- --spec "$SH/$id.json" --device "$DEV" --final --out "$RAW/$id" --width 1920 --samples 96 --noise 0.01 --frames $f0 $f1 --pass $pass < /dev/null
  cp "$RAW/$id/${id}_${pass}_meta.json" "$OUT/meta_${id}_${pass}.json"
done <<< "$JOBS"
wait
while read -r id f0 f1 pass; do
  [ -z "$id" ] && continue
  n=$((f1 - f0 + 1))
  if [ "$pass" = "beauty" ]; then
    ffmpeg -nostdin -v error -y -framerate 24 -start_number $f0 -i "$RAW/$id/beauty/%04d.png" -frames:v $n -c:v libx264 -crf 14 -preset slow -pix_fmt yuv420p -movflags +faststart "$OUT/${id}_1920_final.mp4"
    for k in 0 1 2 3 4; do fr=$((f0 + (n - 1) * k / 4)); ffmpeg -nostdin -v error -y -i "$RAW/$id/beauty/$(printf %04d $fr).png" -q:v 2 "$OUT/${id}_key${k}.jpg"; done
  fi
  if [ "$id" = "R01_PORTRAIT_R4" ]; then
    mkdir -p "$OUT/${id}_${pass}_frames"
    # beauty as high-quality JPEG, the matte as its alpha channel only (grey PNG): small enough to bring back
    for f in $(seq $f0 $f1); do src="$RAW/$id/$pass/$(printf %04d $f).png"; dst="$OUT/${id}_${pass}_frames/$(printf %04d $f)"
      if [ "$pass" = matte ]; then ffmpeg -nostdin -v error -y -i "$src" -vf alphaextract -pix_fmt gray "$dst.png"
      else ffmpeg -nostdin -v error -y -i "$src" -q:v 1 "$dst.jpg"; fi; done
  fi
done <<< "$JOBS"
python3 - "$OUT" <<'PY'
import glob, json, os, sys
out = sys.argv[1]; rows = []
for m in sorted(glob.glob(os.path.join(out, "meta_*.json"))):
    j = json.load(open(m))
    for fr in j["frames"]:
        if "render_s" in fr: rows.append({"job": os.path.basename(m)[5:-5], "res": j["res"], "s": fr["render_s"], "devices": j["devices"]})
json.dump(rows, open(os.path.join(out, "timings.json"), "w"), indent=1)
print(len(rows), "frames", round(sum(r["s"] for r in rows) / 60, 1), "min")
PY
