#!/usr/bin/env bash
# Creates a new JUCE plug-in project with the build-and-download pipeline already in it.
#
#   bash new-plugin.sh "Product Name" [--effect] [--dir path] [--code Abcd]
#                      [--company "Name"] [--manufacturer Abcd] [--bundle-prefix com.you]
#
# Instrument by default (--effect for an audio effect). Your company, manufacturer code and
# bundle prefix are the same for all your plug-ins: set them once in your shell profile as
# PLUGIN_COMPANY, PLUGIN_MANUFACTURER and PLUGIN_BUNDLE_PREFIX, or pass them each time.
set -euo pipefail

kit="$(cd "$(dirname "$0")" && pwd)"
product=""
effect=0
dir=""
code=""
company="${PLUGIN_COMPANY:-My Company}"
manufacturer="${PLUGIN_MANUFACTURER:-Myco}"
bundlePrefix="${PLUGIN_BUNDLE_PREFIX:-com.mycompany}"
while [ $# -gt 0 ]; do
    case "$1" in
        --effect) effect=1; shift ;;
        --dir) dir="$2"; shift 2 ;;
        --code) code="$2"; shift 2 ;;
        --company) company="$2"; shift 2 ;;
        --manufacturer) manufacturer="$2"; shift 2 ;;
        --bundle-prefix) bundlePrefix="$2"; shift 2 ;;
        -h | --help) sed -n '2,10p' "$0"; exit 0 ;;
        *) product="$1"; shift ;;
    esac
done
[ -n "$product" ] || { echo "usage: bash new-plugin.sh \"Product Name\" [--effect] [--dir path] ..." >&2; exit 2; }

# A CMake target and bundle id from the name ("Tape Bloom" -> TapeBloom, com.you.tapebloom).
target="$(printf '%s' "$product" | sed -E 's/[^A-Za-z0-9]+/ /g' | awk '{for (i = 1; i <= NF; i++) printf "%s", toupper(substr($i, 1, 1)) substr($i, 2)}')"
case "$target" in [0-9]*) target="P$target" ;; esac
[ -n "$target" ] || { echo "error: the name needs some letters or digits." >&2; exit 1; }
lower="$(printf '%s' "$target" | tr '[:upper:]' '[:lower:]')"
bundleId="$bundlePrefix.$lower"
# The plug-in code: four characters, unique among your plug-ins (first letter capital).
if [ -z "$code" ]; then
    code="$(printf '%sxxxx' "$lower" | cut -c1-4)"
    code="$(printf '%s' "${code:0:1}" | tr '[:lower:]' '[:upper:]')${code:1}"
fi
for c in "$code" "$manufacturer"; do
    if ! printf '%s' "$c" | grep -qE '^[A-Za-z0-9]{4}$'; then
        echo "error: plug-in and manufacturer codes are exactly 4 letters or digits (got \"$c\")." >&2
        exit 1
    fi
done
if ! printf '%s' "$manufacturer" | grep -q '[A-Z]'; then
    echo "error: the manufacturer code needs at least one capital letter (Apple reserves all-lowercase codes)." >&2
    exit 1
fi

if [ "$effect" = 1 ]; then
    isSynth=FALSE; vst3="Fx"; auType=kAudioUnitType_Effect
else
    isSynth=TRUE; vst3="Instrument Synth"; auType=kAudioUnitType_MusicDevice
fi

dir="${dir:-$target}"
if [ -e "$dir" ] && [ -n "$(ls -A "$dir" 2> /dev/null)" ]; then
    echo "error: $dir already exists and is not empty." >&2
    exit 1
fi
mkdir -p "$dir"
cp -R "$kit/starter/." "$dir/"
mv "$dir/gitignore" "$dir/.gitignore"

# Fill in the names. (sed -i differs between macOS and Linux: write through a temp file.)
fill()
{
    local tmp
    tmp="$(mktemp)"
    sed -e "s|@TARGET@|$target|g" -e "s|@PRODUCT@|$product|g" -e "s|@COMPANY@|$company|g" \
        -e "s|@BUNDLE_ID@|$bundleId|g" -e "s|@MANUFACTURER_CODE@|$manufacturer|g" \
        -e "s|@PLUGIN_CODE@|$code|g" -e "s|@IS_SYNTH@|$isSynth|g" -e "s|@VST3_CATEGORY@|$vst3|g" \
        -e "s|@AU_MAIN_TYPE@|$auType|g" "$1" > "$tmp"
    mv "$tmp" "$1"
}
fill "$dir/CMakeLists.txt"

if [ ! -d "$dir/.git" ] && command -v git > /dev/null; then
    git -C "$dir" init -q -b main 2> /dev/null || git -C "$dir" init -q
fi

bash "$kit/adopt.sh" "$dir" --target "$target" > /dev/null
cat <<EOF
Created $dir: $product ($( [ "$effect" = 1 ] && echo effect || echo instrument ))
  target $target, bundle id $bundleId, codes $manufacturer / $code, company "$company"

Build and try it on your Mac:
  cd "$dir" && bash packaging/ci/build-local.sh
  open dist     # double-click Install.command inside the zip's folder

Get downloads from GitHub on every push:
  create an empty repository on github.com, then
  cd "$dir" && git add -A && git commit -m "First version" &&
  git remote add origin https://github.com/<you>/<repo>.git && git push -u origin main
EOF
if [ "$manufacturer" = Myco ]; then
    echo
    echo "Note: company and manufacturer code are placeholders (My Company / Myco). Set yours"
    echo "before the first release: export PLUGIN_COMPANY=\"...\" PLUGIN_MANUFACTURER=Abcd"
    echo "PLUGIN_BUNDLE_PREFIX=com.you in your shell profile, or edit CMakeLists.txt now."
fi
