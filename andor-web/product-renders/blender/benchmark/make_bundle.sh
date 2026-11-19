#!/bin/bash
# Builds andor_render_benchmark.zip (the scene, one shot, the CPU reference) from the repository.
# usage: benchmark/make_bundle.sh <out dir> [<reference frames dir> <reference timings json>]
set -e
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
OUT=${1:?out dir}; REF=${2:-}; REFT=${3:-}
B=$(mktemp -d)
mkdir -p "$B/product-renders/blender/scripts" "$B/product-renders/blender/config" "$B/product-renders/blender/textures_current" "$B/tools/blender-assets/config" "$B/reference/frames"
cp "$REPO/andor-web/product-renders/blender/scripts/andor_scene.py" "$REPO/andor-web/product-renders/blender/scripts/motion_preview.py" "$B/product-renders/blender/scripts/"
cp "$REPO/andor-web/product-renders/blender/config/campaign_setups.json" "$B/product-renders/blender/config/"
cp "$REPO/andor-web/product-renders/blender/textures_current/"* "$B/product-renders/blender/textures_current/"
cp "$REPO/tools/blender-assets/config/macro-knob.json" "$B/tools/blender-assets/config/"
cp "$REPO/andor-web/product-renders/blender/benchmark/README.md" "$REPO/andor-web/product-renders/blender/benchmark/compare_reference.py" "$B/"
[ -n "$REF" ] && cp "$REF"/*.png "$B/reference/frames/"
[ -n "$REFT" ] && cp "$REFT" "$B/reference/"
(cd "$B" && sha256sum $(find . -type f ! -name SHA256SUMS | sort) > SHA256SUMS)
mkdir -p "$OUT"; rm -f "$OUT/andor_render_benchmark.zip"
(cd "$B" && zip -qr "$OUT/andor_render_benchmark.zip" .)
rm -rf "$B"
ls -la "$OUT/andor_render_benchmark.zip"
