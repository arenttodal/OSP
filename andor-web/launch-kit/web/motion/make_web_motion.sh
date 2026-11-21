#!/bin/bash
# Website delivery versions of the final-quality films. The masters (CRF 14, 4-8 Mbit/s) are for editing and
# archive; these are what the page streams.
#   <film>_<w>.mp4       H.264 High, yuv420p, faststart, no audio: plays everywhere (854 for phones, 1280, 1920)
#   <film>_<w>_av1.mp4   AV1 (SVT-AV1) at 1280 and 1920: about 40 % smaller where the browser decodes AV1;
#                        the page offers it first and falls back to H.264 (motion.js)
#   <film>_poster.{webp,jpg}  the first frame (shown until the film starts, so there is no jump)
#   <film>_still.{webp,jpg}   a representative frame for reduced-motion and data-saver visitors
# Keyframes every 2 s for quick starts; the bitrate is capped so a slow phone connection does not stall.
# usage: make_web_motion.sh <masters dir (01_final-quality_1920)> <out dir>
set -euo pipefail
M=${1:?masters}; O=${2:?out}; mkdir -p "$O"
# film id | master (relative to M) | still frame (seconds)
FILMS="R01_CUT-R4|R01_reveal/andor-osp_R01_CUT-R4_16x9_1920w_24fps_final_v03.mp4|11.9
R01_CUT-R4-type-t1|R01_reveal/andor-osp_R01_CUT-R4-type-t1_16x9_1920w_24fps_final_v03.mp4|11.9
R04_WIN16|R04_window-light/andor-osp_R04_WIN16_16x9_1920w_24fps_final_v03.mp4|8
D2_TRACK|D2_macro-row/andor-osp_D2_TRACK_16x9_1920w_24fps_final_v03.mp4|6
R03_GLIDE-edit-15s5|R03_immersive-glide/andor-osp_R03_GLIDE-edit-15s5_16x9_1920w_24fps_final_v03.mp4|7.75
R02_DIM-R4-loop|R02_silhouette/andor-osp_R02_DIM-R4-loop_16x9_1920w_24fps_final_v03.mp4|5.5"
H264="-c:v libx264 -preset slow -profile:v high -pix_fmt yuv420p -g 48 -keyint_min 48 -sc_threshold 0 -movflags +faststart -an"
while IFS='|' read -r id src still; do
  in="$M/$src"
  for spec in "1920 22 5000k" "1280 23 2500k" "854 24 1200k"; do
    set -- $spec
    ffmpeg -nostdin -v error -y -i "$in" -vf "scale=$1:-2:flags=lanczos" $H264 -crf $2 -maxrate $3 -bufsize $(( ${3%k} * 2 ))k "$O/${id}_$1.mp4"
  done
  for spec in "1920 34" "1280 36"; do
    set -- $spec
    ffmpeg -nostdin -v error -y -i "$in" -vf "scale=$1:-2:flags=lanczos" -c:v libsvtav1 -preset 6 -crf $2 -g 48 \
      -pix_fmt yuv420p -svtav1-params tune=0 -movflags +faststart -an "$O/${id}_$1_av1.mp4"
  done
  ffmpeg -nostdin -v error -y -i "$in" -frames:v 1 -vf "scale=1920:-2" -q:v 3 "$O/${id}_poster.jpg"
  ffmpeg -nostdin -v error -y -ss "$still" -i "$in" -frames:v 1 -vf "scale=1920:-2" -q:v 3 "$O/${id}_still.jpg"
  for k in poster still; do
    ffmpeg -nostdin -v error -y -i "$O/${id}_$k.jpg" -c:v libwebp -quality 80 "$O/${id}_$k.webp"
  done
  echo "$id done"
done <<< "$FILMS"
# summary: size and bitrate of every file
for f in "$O"/*.mp4; do
  printf "%-44s %6.2f MB  %5.0f kbit/s\n" "$(basename "$f")" "$(echo "$(stat -c %s "$f") / 1048576" | bc -l)" \
    "$(ffprobe -v error -show_entries format=bit_rate -of csv=p=0 "$f" | awk '{print $1/1000}')"
done
