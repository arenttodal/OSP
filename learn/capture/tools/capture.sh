#!/usr/bin/env bash
# ANDOR Learn Capture Studio: capture and post-process scenes (development only).
#
#   cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON -DOSP_BUILD_LEARN_CAPTURE=ON
#   cmake --build build-plugin --target osp_learn_capture
#   learn/capture/tools/capture.sh                 # every scene in learn/capture/scenarios
#   learn/capture/tools/capture.sh a-load-and-play c2-grab-last
#
# Needs: Xvfb, openbox, xprop (x11-utils), ffmpeg, python3 with Pillow.
# Writes masters to learn/capture/out/<scene>/ (git-ignored) and web assets to learn/assets/osp/<scene>/.
# A scene whose runner or validation fails produces no accepted asset; the script exits non-zero.
set -uo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
runner="${LEARN_CAPTURE_RUNNER:-build-plugin/learn/capture/runner/osp_learn_capture}"
commit="$(git rev-parse --short=7 HEAD)"
source_commit="${LEARN_SOURCE_COMMIT:-$(git merge-base HEAD origin/claude/macos-release 2>/dev/null | cut -c1-7)}"
scenes=("$@")
if [ ${#scenes[@]} -eq 0 ]; then
  for f in learn/capture/scenarios/*.json; do scenes+=("$(basename "$f" .json)"); done
fi
python3 learn/capture/tools/make-sounds.py > /dev/null   # basic sounds (saw, sine, pluck), identical every run
failed=0
for s in "${scenes[@]}"; do
  out="learn/capture/out/$s"
  rm -rf "$out/master.mkv" "$out/frames.json" "$out/run.json" "$out/post-report.json" "$out/failure-last-frame.png"
  echo "== $s"
  if ! learn/capture/tools/xvfb-wm.sh "$runner" --scenario "learn/capture/scenarios/$s.json" --out "$out" \
       --fixtures learn/capture/fixtures > "$out.log" 2>&1; then
    echo "   capture FAILED: $(grep -m1 FAILED "$out.log")"; failed=1; continue
  fi
  if ! python3 learn/capture/tools/post.py --run "$out" --scenario "learn/capture/scenarios/$s.json" \
       --assets learn/assets/osp --commit "$source_commit"; then
    echo "   validation FAILED"; failed=1; continue
  fi
done
python3 learn/capture/tools/manifest.py learn/assets/osp "$source_commit" "$commit"
exit $failed
