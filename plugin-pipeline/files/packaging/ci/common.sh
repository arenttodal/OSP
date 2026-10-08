#!/usr/bin/env bash
# Shared by the pipeline scripts: reads packaging/pipeline.conf and finds the plug-ins the
# build made. Source it; it sets: root, build, artefacts, bundles[], product, safe, version.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
# shellcheck source=../pipeline.conf
source packaging/pipeline.conf
build="${BUILD_DIR:-build}"

findArtefacts()
{
    local pattern="${PLUGIN_TARGET:+${PLUGIN_TARGET}_artefacts}"
    pattern="${pattern:-*_artefacts}"
    local found
    found="$(find "$build" -type d -name "$pattern" -not -path "*/_deps/*" 2>/dev/null || true)"
    local count
    count="$(printf '%s\n' "$found" | grep -c . || true)"
    if [ "$count" != 1 ]; then
        echo "error: expected one plug-in folder ($pattern) in $build, found $count." >&2
        if [ -n "$found" ]; then printf '  %s\n' $found >&2; fi
        echo "Set PLUGIN_TARGET in packaging/pipeline.conf to the juce_add_plugin target." >&2
        return 1
    fi
    # Multi-config generators (Visual Studio, Xcode) add a Release folder.
    if [ -d "$found/Release" ]; then echo "$found/Release"; else echo "$found"; fi
}

artefacts="$(findArtefacts)"

bundles=()
shopt -s nullglob
for b in "$artefacts"/AU/*.component "$artefacts"/VST3/*.vst3 "$artefacts"/CLAP/*.clap \
         "$artefacts"/Standalone/*.app "$artefacts"/Standalone/*.exe; do
    bundles+=("$b")
done
# Linux: the standalone is a bare executable.
if [ "$(uname -s)" = Linux ]; then
    for b in "$artefacts"/Standalone/*; do
        if [ -f "$b" ] && [ -x "$b" ]; then bundles+=("$b"); fi
    done
fi
shopt -u nullglob
if [ "${#bundles[@]}" = 0 ]; then
    echo "error: no plug-ins in $artefacts (AU, VST3, CLAP or Standalone)." >&2
    exit 1
fi

if [ -n "${PRODUCT:-}" ]; then
    product="$PRODUCT"
else
    first="$(basename "${bundles[0]}")"
    product="${first%.*}"
fi
safe="$(printf '%s' "$product" | sed 's/[^A-Za-z0-9._-]/-/g')"
version="$(git rev-parse --short HEAD 2>/dev/null || echo local)"
