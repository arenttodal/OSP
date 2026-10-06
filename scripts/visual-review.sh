#!/bin/sh
# Renders the canonical scenes at 1448 x 1086 into design/current/ and compares them with
# design/reference/ (scripts/visual-compare.py). Linux needs xvfb-run; on macOS run the
# test binary directly. Usage: scripts/visual-review.sh [build-dir]   (default build-plugin)
set -e
cd "$(dirname "$0")/.."
build="${1:-build-plugin}"
cmake --build "$build" --target osp_plugin_tests
mkdir -p design/current
runner=""
command -v xvfb-run >/dev/null 2>&1 && runner="xvfb-run -a"
OSP_SNAPSHOT_DIR="$PWD/design/current" $runner "$build/apps/plugin/osp_plugin_tests" "[canonical]"
python3 scripts/visual-compare.py
