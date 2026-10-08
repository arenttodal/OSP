#!/usr/bin/env bash
# Adds the build-and-download pipeline to an existing JUCE (CMake) plug-in repository.
#
#   bash adopt.sh <path/to/plugin-repo> [--target <juce_add_plugin target>] [--cmake-args "<flags>"] [--force]
#
# It finds the juce_add_plugin(...) target, copies the workflow and packaging scripts in,
# writes packaging/pipeline.conf, and says what (if anything) the project still needs.
# Existing files are kept unless --force. Nothing is committed or pushed.
set -euo pipefail

kit="$(cd "$(dirname "$0")" && pwd)"
repo=""
target=""
cmakeArgs=""
force=0
while [ $# -gt 0 ]; do
    case "$1" in
        --target) target="$2"; shift 2 ;;
        --cmake-args) cmakeArgs="$2"; shift 2 ;;
        --force) force=1; shift ;;
        -h | --help) sed -n '2,9p' "$0"; exit 0 ;;
        *) repo="$1"; shift ;;
    esac
done
[ -n "$repo" ] || { echo "usage: bash adopt.sh <path/to/plugin-repo> [--target Name] [--cmake-args \"...\"] [--force]" >&2; exit 2; }
repo="$(cd "$repo" && pwd)"
[ -f "$repo/CMakeLists.txt" ] || { echo "error: $repo has no CMakeLists.txt. The pipeline builds JUCE CMake projects (see README: Projucer projects)." >&2; exit 1; }

# The plug-in targets (outside build folders and any vendored copy of JUCE).
targets="$(grep -rhoE --include=CMakeLists.txt --include='*.cmake' \
    --exclude-dir=.git --exclude-dir='build*' --exclude-dir='cmake-build-*' --exclude-dir=_deps \
    --exclude-dir=JUCE --exclude-dir=juce --exclude-dir=node_modules --exclude-dir=dist \
    'juce_add_plugin[[:space:]]*\([[:space:]]*[A-Za-z0-9_]+' "$repo" 2> /dev/null \
    | sed -E 's/juce_add_plugin[[:space:]]*\([[:space:]]*//' | sort -u || true)"
count="$(printf '%s\n' "$targets" | grep -c . || true)"
if [ -z "$target" ]; then
    if [ "$count" = 0 ]; then
        echo "error: no juce_add_plugin(...) found in $repo. Pass --target if it is built another way." >&2
        exit 1
    elif [ "$count" -gt 1 ]; then
        echo "Several plug-in targets found; choose one with --target:" >&2
        printf '  %s\n' $targets >&2
        exit 1
    fi
    target="$targets"
fi
echo "Plug-in target: $target"

# Copy the pipeline in.
copied=()
kept=()
while IFS= read -r -d '' src; do
    rel="${src#"$kit/files/"}"
    dest="$repo/$rel"
    if [ "$rel" = packaging/pipeline.conf ]; then continue; fi
    if [ -e "$dest" ] && [ "$force" = 0 ]; then
        kept+=("$rel")
        continue
    fi
    mkdir -p "$(dirname "$dest")"
    cp "$src" "$dest"
    case "$dest" in *.sh | *.command) chmod +x "$dest" ;; esac
    copied+=("$rel")
done < <(find "$kit/files" -type f -print0)

cmakeFiles="$(find "$repo" \( -name CMakeLists.txt -o -name '*.cmake' \) -not -path '*/.git/*' \
    -not -path '*/build*' -not -path '*/_deps/*' -not -path '*/JUCE/*' 2> /dev/null)"

# A build switch such as option(MY_BUILD_PLUGIN "..." OFF): the CI turns it on.
pluginOption="$(printf '%s\n' "$cmakeFiles" | xargs grep -hoE 'option[[:space:]]*\([[:space:]]*[A-Za-z0-9_]*PLUGIN[A-Za-z0-9_]*[[:space:]].*[[:space:]]OFF[[:space:]]*\)' 2> /dev/null \
    | sed -E 's/option[[:space:]]*\([[:space:]]*([A-Za-z0-9_]+).*/\1/' | sort -u | head -1 || true)"
autoArgs=0
if [ -n "$pluginOption" ] && [ -z "$cmakeArgs" ]; then
    cmakeArgs="-D$pluginOption=ON"
    autoArgs=1
fi

conf="$repo/packaging/pipeline.conf"
if [ -e "$conf" ] && [ "$force" = 0 ]; then
    kept+=(packaging/pipeline.conf)
else
    sed -e "s|@PLUGIN_TARGET@|$target|" -e "s|@CMAKE_ARGS@|$cmakeArgs|" "$kit/files/packaging/pipeline.conf" > "$conf"
    copied+=(packaging/pipeline.conf)
fi

# dist/ is where packages are made locally; keep it out of git.
if ! grep -qsx '/dist/' "$repo/.gitignore" && ! grep -qsx 'dist/' "$repo/.gitignore"; then
    printf '\n# plug-in packages (packaging/ci/package.sh)\n/dist/\n' >> "$repo/.gitignore"
    copied+=(".gitignore (added /dist/)")
fi

echo
echo "Added:"
for f in "${copied[@]+"${copied[@]}"}"; do echo "  $f"; done
if [ "${#kept[@]}" -gt 0 ]; then
    echo "Kept as they were (use --force to replace):"
    for f in "${kept[@]}"; do echo "  $f"; done
fi

# What the CI needs from the project.
echo
echo "Checks:"
juceBy="unknown"
if printf '%s\n' "$cmakeFiles" | xargs grep -qiE 'FetchContent_Declare[[:space:]]*\([[:space:]]*juce|CPMAddPackage.*juce' 2> /dev/null; then
    juceBy=fetch
elif printf '%s\n' "$cmakeFiles" | xargs grep -qiE 'add_subdirectory[[:space:]]*\(.*juce' 2> /dev/null; then
    juceBy=subdirectory
elif printf '%s\n' "$cmakeFiles" | xargs grep -qE 'find_package[[:space:]]*\([[:space:]]*JUCE' 2> /dev/null; then
    juceBy=installed
fi
case "$juceBy" in
    fetch) echo "  ok    JUCE is downloaded by CMake (FetchContent)." ;;
    subdirectory)
        if grep -qsi juce "$repo/.gitmodules"; then
            echo "  ok    JUCE is a git submodule (the CI checks submodules out)."
        else
            echo "  check JUCE comes from add_subdirectory(...): it must be committed in the repo or a"
            echo "        submodule, or the CI cannot find it. Simplest: FetchContent (README, step 'JUCE')."
        fi
        ;;
    installed)
        echo "  FIX   find_package(JUCE): the CI machines have no JUCE installed. Use FetchContent"
        echo "        instead (README, step 'JUCE')."
        ;;
    *) echo "  check Could not tell how JUCE is found; the CI needs to get it without help." ;;
esac
if [ -n "$pluginOption" ]; then
    if [ "$autoArgs" = 1 ]; then
        echo "  ok    $pluginOption defaults to OFF: CMAKE_ARGS turns it on (-D$pluginOption=ON)."
    else
        echo "  check $pluginOption defaults to OFF: if it switches the plug-in on, CMAKE_ARGS in"
        echo "        packaging/pipeline.conf needs -D$pluginOption=ON."
    fi
fi
if [ ! -d "$repo/.git" ]; then
    echo "  note  $repo is not a git repository yet (git init, then push it to GitHub)."
fi

echo
echo "Next:"
echo "  1. Review packaging/pipeline.conf."
echo "  2. Optional, on your Mac:  bash packaging/ci/build-local.sh   (same build, zip in dist/)"
echo "  3. Commit and push. The run's page (GitHub > Actions > Plugin build) has the download;"
echo "     the newest build of each branch is also at"
echo "     https://github.com/<you>/<repo>/releases/download/test-<branch>/<Product>-macOS.zip"
