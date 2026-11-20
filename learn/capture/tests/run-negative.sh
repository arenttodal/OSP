#!/usr/bin/env bash
# Each negative scenario must make the runner fail (non-zero) and post.py write no asset.
cd "$(dirname "$0")/../../.."
runner=build-plugin/learn/capture/runner/osp_learn_capture
rc=0
for f in learn/capture/tests/negative-*.json; do
  s=$(basename "$f" .json); out=learn/capture/out/tests/$s
  rm -rf "$out"; mkdir -p "$out"
  if learn/capture/tools/xvfb-wm.sh $runner --scenario "$f" --out "$out" > "$out.log" 2>&1; then
    echo "$s: runner PASSED (wrong)"; rc=1; continue
  fi
  python3 learn/capture/tools/post.py --run "$out" --scenario "$f" --assets learn/capture/out/tests/assets > /dev/null 2>&1
  if [ -e "learn/capture/out/tests/assets/${s#test-}" ] && ls learn/capture/out/tests/assets/*/*.mp4 >/dev/null 2>&1; then
    echo "$s: assets written (wrong)"; rc=1; continue
  fi
  echo "$s: failed as expected — $(python3 -c "import json;print(json.load(open('$out/run.json'))['failure'])")"
done
exit $rc
