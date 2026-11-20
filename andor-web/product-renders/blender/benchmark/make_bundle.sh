#!/bin/bash
# Builds andor_render_benchmark.zip from the repository: the scene, the R4 shot rig (cine_rig.py) with R04 Window
# Light (the first final-quality benchmark), the frozen interface (textures_0.9.0-beta.1 + FROZEN.json; final renders
# refuse any other) and the CPU reference. The CPU reference frames passed in must be rendered with the same spec.
# usage: benchmark/make_bundle.sh <out dir> [<reference frames dir> <reference timings json>]
set -e
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
OUT=${1:?out dir}; REF=${2:-}; REFT=${3:-}
B=$(mktemp -d)
# the layout mirrors the repository (andor-web/ and tools/ side by side): andor_scene.py finds the knob
# module at ../../../../tools/blender-assets from its own folder
A="$B/andor-web"
mkdir -p "$A/product-renders/blender/scripts" "$A/product-renders/blender/cine/shots" "$A/product-renders/blender/config" "$A/product-renders/blender/textures_0.9.0-beta.1" "$B/tools/blender-assets/config" "$B/tools/blender-assets/osp_assets" "$B/reference/frames"
cp "$REPO/andor-web/product-renders/blender/scripts/andor_scene.py" "$REPO/andor-web/product-renders/blender/scripts/motion_preview.py" "$A/product-renders/blender/scripts/"
cp "$REPO/andor-web/product-renders/blender/cine/cine_rig.py" "$A/product-renders/blender/cine/"
cp "$REPO/andor-web/product-renders/blender/cine/shots/R04_WIN16.json" "$A/product-renders/blender/cine/shots/"
cp "$REPO/andor-web/product-renders/blender/config/campaign_setups.json" "$A/product-renders/blender/config/"
cp "$REPO/andor-web/product-renders/blender/textures_0.9.0-beta.1/"* "$A/product-renders/blender/textures_0.9.0-beta.1/"
cp "$REPO/tools/blender-assets/config/macro-knob.json" "$B/tools/blender-assets/config/"
cp "$REPO/tools/blender-assets/osp_assets/"*.py "$B/tools/blender-assets/osp_assets/"
cp "$REPO/andor-web/product-renders/blender/benchmark/README.md" "$REPO/andor-web/product-renders/blender/benchmark/compare_reference.py" "$REPO/andor-web/product-renders/blender/benchmark/run_benchmark.sh" "$B/"
[ -n "$REF" ] && cp "$REF"/*.png "$B/reference/frames/"
[ -n "$REFT" ] && cp "$REFT" "$B/reference/"
(cd "$B" && sha256sum $(find . -type f ! -name SHA256SUMS | sort) > SHA256SUMS)
mkdir -p "$OUT"; rm -f "$OUT/andor_render_benchmark.zip"
(cd "$B" && zip -qr "$OUT/andor_render_benchmark.zip" .)
rm -rf "$B"
ls -la "$OUT/andor_render_benchmark.zip"
