#!/usr/bin/env bash
# Re-takes every website capture from the real OSP editor (headless, ceramic look).
# Run from the OSP repository root after building the plugin tests:
#   cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && ninja -C build-plugin osp_plugin_tests
#   andor-web/tools/capture.sh                       # stand-in sources (concept stage)
#   OSP_WEB_SOURCES="a.wav;b.wav;c.wav" andor-web/tools/capture.sh   # approved recordings (launch)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
osp="$(cd "$here/.." && pwd)"
out="$here/assets/source/captures"
mkdir -p "$out"
OSP_WEB_CAPTURE="$out" xvfb-run -a "$osp/build-plugin/apps/plugin/osp_plugin_tests" "[web-capture]"
echo "commit: $(git -C "$osp" rev-parse --short HEAD)  (record it in assets/manifests/assets.json)"
node "$here/tools/build-assets.mjs"
