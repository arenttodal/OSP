# Plug-in build pipeline: hand-off for an AI coding agent

You are setting up a build pipeline for a JUCE audio plug-in. When it's done, every `git push`
makes GitHub Actions:

1. build the plug-in on macOS (universal: Apple Silicon and Intel) and Windows;
2. run its tests;
3. validate the AU with Apple's `auval`;
4. pack a zip with a one-click installer (`Install.command` on macOS, `Install.bat` on Windows).

The download appears on the run's page. A fixed link per branch also always gets the newest
build:
`https://github.com/<owner>/<repo>/releases/download/test-<branch>/<Product>-macOS.zip`.

The user tests a build by downloading it, double-clicking `Install.command` and restarting
their DAW. Nothing else is needed.

## What to do

1. **Recreate the kit.** Write every file under "The kit" below into a folder named
   `plugin-pipeline/`, byte for byte, at the paths given. Put it somewhere outside the plug-in
   repository, or delete it afterwards. Then make the scripts executable:
   `chmod +x plugin-pipeline/*.sh plugin-pipeline/files/packaging/ci/*.sh plugin-pipeline/files/packaging/macos/install.command plugin-pipeline/files/packaging/linux/install.sh`
2. **Either** add it to the user's existing plug-in repository:
   `bash plugin-pipeline/adopt.sh <repo> [--target <juce_add_plugin target>] [--cmake-args "<flags>"]`
   **or** create a new plug-in:
   `bash plugin-pipeline/new-plugin.sh "Product Name" [--effect] [--dir <path>] [--company "..."] [--manufacturer Abcd] [--code Abcd] [--bundle-prefix com.name]`
3. **Read what `adopt.sh` reports under "Checks" and fix what it flags.**
   - Above all: the CI must be able to get JUCE without help. FetchContent, a git submodule
     or a committed copy all work. `find_package(JUCE)` does not: replace it with
     FetchContent (snippet below).
   - A `*PLUGIN*` CMake option that defaults to OFF is turned on in `CMAKE_ARGS`
     automatically. Check that this is the right switch.
4. **Review `packaging/pipeline.conf`** in the repository. Every setting is explained in the
   file.
5. **Build locally if you can** (`bash packaging/ci/build-local.sh`; on Linux, run it under
   `xvfb-run -a`). It does the CI's configure, build, test and pack, and on a Mac also auval.
   The zip lands in `dist/`.
6. **Commit and push** to the user's branch, if they asked you to. Then watch the "Plugin build"
   workflow and fix any failure: read the job log; don't guess.
7. **Give the user the run's download link** (in the run summary) and the fixed link.

## Rules and gotchas

- **Identity codes.**
  - `PLUGIN_MANUFACTURER_CODE` is 4 characters with at least one capital. It's the user's
    own code, the same for all their plug-ins.
  - `PLUGIN_CODE` is 4 characters, unique per plug-in.
  - Never change either, or `BUNDLE_ID`, on a plug-in people already use: hosts identify
    plug-ins and saved sessions by them.
  - Ask the user for their company name, manufacturer code and bundle prefix rather than
    leaving the placeholders `My Company` / `Myco` / `com.mycompany`.
- **Scripts.** They are bash and must keep working on macOS's bash 3.2: no `mapfile`, no
  `${var,,}`, no associative arrays. Under `set -u`, write `${a[@]+"${a[@]}"}` for arrays
  that may be empty.
- **Windows.** The batch files need CRLF line ends; `package.sh` converts them when packing.
  Don't commit them with CRLF.
- **Downloads.** A run's download is a zip holding the zip, because GitHub zips artifacts
  again. The inner zip is made with `ditto`, which keeps the bundles' executable bits and
  symlinks. Don't upload the plug-in folders directly: the bits would be lost.
- **Signing.**
  - The builds are unsigned test builds. `package.sh` gives each bundle a full ad-hoc
    signature (Apple Silicon only loads signed code).
  - `Install.command` clears the download quarantine.
  - Releasing to the public needs an Apple Developer ID, `codesign`, `notarytool` and a
    `.pkg`. That's a separate step this kit doesn't do.
- **The fixed link** is a GitHub pre-release named `test-<branch>`, updated by
  `packaging/ci/latest-link.sh`. It needs `permissions: contents: write`; the workflow
  already declares it. `LATEST_LINK=0` turns it off.
- **Cost.** In private repositories macOS minutes count 10× against the Actions allowance.
  `WINDOWS=0` saves the Windows build; a newer push cancels the build it replaces.
- **Projucer projects** (`.jucer`, no CMakeLists.txt) need a CMake build first. Start from
  `new-plugin.sh` and move their `Source/` files in.

JUCE through FetchContent (instead of `find_package(JUCE)`):

```cmake
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                          GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
```

## Layout

```
plugin-pipeline/
  adopt.sh, new-plugin.sh     the two entry points
  files/                      copied into a plug-in repository by adopt.sh:
    .github/workflows/plugin-build.yml
    packaging/pipeline.conf   (written with the detected target and CMake flags)
    packaging/ci/             common.sh, build-local.sh, package.sh, validate-macos.sh, latest-link.sh
    packaging/macos|windows|linux/   installers and read-mes put into the zip
  starter/                    the new-plug-in project (new-plugin.sh fills in @PLACEHOLDERS@)
```

## The kit

### `plugin-pipeline/README.md`

````markdown
# Plug-in pipeline: push, then download and install with one click

A kit that gives any JUCE plug-in the build OSP has. On every push, GitHub builds the plugin on
real Macs (and Windows) and runs its tests. It then checks the AU the way Logic does and packs a
zip. You install it by double-clicking one file inside the zip.

```
git push  ──►  GitHub Actions (macOS 14, Windows)
                 configure → build (universal arm64 + x86_64) → ctest
                 → pack: AU, VST3, app, Install.command → auval
                 → download on the run's page
                 → and at a fixed link per branch (always the newest build)
```

## Contents

| Path | What it is |
|---|---|
| `adopt.sh` | Adds the pipeline to an **existing** plug-in repository |
| `new-plugin.sh` | Creates a **new** plug-in (instrument or effect) with the pipeline already in it |
| `files/` | What `adopt.sh` copies into a repository (below) |
| `starter/` | The new-plug-in project: CMake, a GAIN parameter, a sine synth or gain effect, an editor, headless tests |

What a repository gets:

| File | Job |
|---|---|
| `.github/workflows/plugin-build.yml` | The CI: macOS and Windows builds, tests, packages, links |
| `packaging/pipeline.conf` | The settings: the **only** file you normally edit |
| `packaging/ci/build-local.sh` | The same build on your own machine (zip in `dist/`) |
| `packaging/ci/package.sh` | Packs the built plug-ins with their installer and read-me |
| `packaging/ci/validate-macos.sh` | auval (codes read from the AU itself), optional pluginval |
| `packaging/ci/latest-link.sh` | Keeps the fixed download link up to date |
| `packaging/macos/install.command` | The one-click installer (per user, no admin password) |
| `packaging/windows/install.bat`, `packaging/linux/install.sh` | The same for Windows and Linux |

## An existing plug-in

```sh
bash plugin-pipeline/adopt.sh ~/code/my-plugin
cd ~/code/my-plugin
git add -A && git commit -m "Build pipeline" && git push
```

`adopt.sh` does the following:
- Finds the `juce_add_plugin(...)` target. With several, pass `--target Name`.
- Copies the files in. Files you already have are kept; `--force` replaces them.
- Writes `packaging/pipeline.conf`.
- Checks what the CI needs:
  - JUCE has to be fetchable (see [JUCE](#juce) below).
  - A build switch such as `option(MY_BUILD_PLUGIN ... OFF)` gets turned on in `CMAKE_ARGS` automatically.

Nothing is committed or pushed for you.

## A new plug-in

```sh
# Once, in ~/.zshrc: your identity, the same for all your plug-ins.
export PLUGIN_COMPANY="Your Name"
export PLUGIN_MANUFACTURER=Abcd          # 4 characters, at least one capital
export PLUGIN_BUNDLE_PREFIX=com.yourname

bash plugin-pipeline/new-plugin.sh "Tape Bloom"            # an instrument
bash plugin-pipeline/new-plugin.sh "Grit Box" --effect     # an audio effect
```

This makes a folder with a working plug-in, its tests and the pipeline. The plug-in code (4
characters, unique per plug-in) comes from the name; set your own with `--code Tblm`. The output
ends with the commands to build it locally and to push it to a new GitHub repository.

Keep the manufacturer code, the plug-in code and the bundle ID once anyone uses the plug-in.
Hosts recognise plug-ins, and the sessions that use them, by those values.

## Every push: downloading and installing

1. GitHub → your repository → **Actions** → the newest **Plugin build** run. Its summary has the
   download links (`<Product>-macOS-<commit>`). Or bookmark the **fixed link**, which always
   gets the newest build of a branch:
   `https://github.com/<you>/<repo>/releases/download/test-main/<Product>-macOS.zip`.
   Other branches use `test-<branch>`.
2. Unzip it. A run's download is a zip holding the zip; Safari opens both. The fixed link is a
   single zip.
3. Double-click **Install.command**. If macOS refuses, right-click it, choose Open, then Open.
4. Restart the DAW.

The installer puts the plug-ins in `~/Library/Audio/Plug-Ins/...` and the app in
`~/Applications`. It clears the download quarantine, since these test builds are not
notarised, and makes Logic rescan.

## Settings: `packaging/pipeline.conf`

| Setting | Default | Meaning |
|---|---|---|
| `PRODUCT` | bundle name | The name on the download |
| `PLUGIN_TARGET` | detected | The `juce_add_plugin` target (finds its `<target>_artefacts`) |
| `CMAKE_ARGS` | detected | Extra configure flags, e.g. `-DMY_BUILD_PLUGIN=ON` |
| `BUILD_TARGET` | everything | Build one target only, e.g. `MyPlugin_All` (faster in big repos) |
| `MACOS_MIN` | `11.0` | Oldest macOS supported |
| `RUN_TESTS` | `1` | Run ctest; a failing test stops the download being made |
| `WINDOWS` | `1` | Also build the Windows VST3 download |
| `LATEST_LINK` | `1` | Keep the fixed link per branch (a pre-release named `test-<branch>`) |
| `PLUGINVAL` | `0` | Also run [pluginval](https://github.com/Tracktion/pluginval) on the VST3 (strict; finds real bugs) |

## Building locally

```sh
bash packaging/ci/build-local.sh     # configure, build, test, pack into dist/ (and auval on a Mac)
```

## JUCE

The CI starts from a clean machine, so CMake has to fetch JUCE itself. A git submodule or a
committed copy works too. If the project uses `find_package(JUCE)` (JUCE installed on your Mac),
replace it with:

```cmake
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                          GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
```

**Projucer projects** (`.jucer`, no CMakeLists.txt) need a CMake build first. The quickest way
is to start from `new-plugin.sh` and move the `Source/` files in.

## Good to know

- **Cost.** Public repositories build free. Private ones use the account's monthly Actions
  minutes, and macOS minutes count 10×. To save minutes:
  - A newer push cancels the build it replaces.
  - `WINDOWS=0` skips Windows.
  - Add `[skip ci]` to a commit message to skip a build.
- **Private repositories.** The downloads need you to be signed in to GitHub. Run downloads are
  kept 90 days.
- **The fixed link** is a GitHub pre-release per branch. It appears under Releases, marked
  pre-release. Delete it any time; the next push recreates it.
- **Releasing to other people** needs an Apple Developer ID: signing, notarising and a `.pkg`
  installer. OSP's `scripts/package-macos.sh` is the template for that step. Test builds don't
  need it.
- **Making this its own repository.** Copy this folder into a new GitHub repository. Optionally,
  mark it as a template under Settings → Template repository.

## What has been checked

| Part | Status |
|---|---|
| `new-plugin.sh`, instrument and effect | Built and tested on Linux with JUCE 8.0.9; all starter checks pass |
| `adopt.sh` | Run on OSP's CMake layout: found `OSP_Plugin` and turned on `OSP_BUILD_PLUGIN` |
| `package.sh` | Packaging run on Linux |
| macOS steps (universal build, ditto, auval, install.command) | Adapted from OSP's CI, which builds and installs this way on every push. The ad-hoc `codesign` and reading the AU codes with PlistBuddy are new, and run for the first time on the first push |
| Windows `install.bat`, pluginval, the fixed link | Written but not yet run: they run for the first time on the first push of a repository using them |
````

### `plugin-pipeline/adopt.sh`

````bash
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
````

### `plugin-pipeline/files/.github/workflows/plugin-build.yml`

````yaml
name: Plugin build

# Builds, tests and packs the plug-in on every push. Each run's page has the downloads
# (macOS: universal AU / VST3 / app with a one-click installer; Windows: VST3). With
# LATEST_LINK=1 the newest build of each branch is also at a fixed link. Settings live in
# packaging/pipeline.conf.

on:
  push:
    branches: ['**']   # not tags: the fixed links move their own "test-*" tags
  pull_request:
  workflow_dispatch:

# A newer push to the same branch cancels the build it replaces.
concurrency:
  group: plugin-build-${{ github.ref }}
  cancel-in-progress: true

permissions:
  contents: write   # only used for the fixed-link pre-release (LATEST_LINK=1)

jobs:
  settings:
    runs-on: ubuntu-latest
    outputs:
      windows: ${{ steps.read.outputs.windows }}
    steps:
      - uses: actions/checkout@v4
      - id: read
        run: |
          source packaging/pipeline.conf
          echo "windows=${WINDOWS:-1}" >> "$GITHUB_OUTPUT"

  macos:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Tools
        run: command -v ninja > /dev/null || brew install ninja

      - name: Configure
        # Universal (Apple Silicon + Intel), so the download runs on any Mac.
        run: |
          source packaging/pipeline.conf
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_MIN:-11.0}" \
            $CMAKE_ARGS

      - name: Build
        run: |
          source packaging/pipeline.conf
          cmake --build build ${BUILD_TARGET:+--target "$BUILD_TARGET"}

      - name: Test
        run: |
          source packaging/pipeline.conf
          if [ "${RUN_TESTS:-1}" = 1 ]; then ctest --test-dir build --output-on-failure --no-tests=ignore; fi

      - name: Package
        run: bash packaging/ci/package.sh

      - name: Validate (auval, optional pluginval)
        run: bash packaging/ci/validate-macos.sh

      - uses: actions/upload-artifact@v4
        id: upload
        with:
          name: ${{ env.ARTIFACT_NAME }}
          path: ${{ env.ARCHIVE }}

      - name: Download link
        run: |
          echo "### macOS download" >> "$GITHUB_STEP_SUMMARY"
          echo "[$ARTIFACT_NAME](${{ steps.upload.outputs.artifact-url }})" >> "$GITHUB_STEP_SUMMARY"

      - name: Fixed link
        if: github.event_name == 'push'
        env:
          GH_TOKEN: ${{ github.token }}
        run: bash packaging/ci/latest-link.sh

  windows:
    needs: settings
    if: needs.settings.outputs.windows == '1'
    runs-on: windows-2022
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure
        run: |
          source packaging/pipeline.conf
          cmake -B build -G "Visual Studio 17 2022" -A x64 $CMAKE_ARGS

      - name: Build
        run: |
          source packaging/pipeline.conf
          cmake --build build --config Release --parallel ${BUILD_TARGET:+--target "$BUILD_TARGET"}

      - name: Test
        run: |
          source packaging/pipeline.conf
          if [ "${RUN_TESTS:-1}" = 1 ]; then ctest --test-dir build -C Release --output-on-failure --no-tests=ignore; fi

      - name: Package
        run: bash packaging/ci/package.sh

      - uses: actions/upload-artifact@v4
        id: upload
        with:
          name: ${{ env.ARTIFACT_NAME }}
          path: ${{ env.ARCHIVE }}

      - name: Download link
        run: |
          echo "### Windows download" >> "$GITHUB_STEP_SUMMARY"
          echo "[$ARTIFACT_NAME](${{ steps.upload.outputs.artifact-url }})" >> "$GITHUB_STEP_SUMMARY"

      - name: Fixed link
        if: github.event_name == 'push'
        env:
          GH_TOKEN: ${{ github.token }}
        run: bash packaging/ci/latest-link.sh
````

### `plugin-pipeline/files/packaging/ci/build-local.sh`

````bash
#!/usr/bin/env bash
# The CI's build on your own machine: configure, build, test and pack into dist/, with the
# same settings (packaging/pipeline.conf). On a Mac it also runs auval.
#   bash packaging/ci/build-local.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/pipeline.conf

generator=()
case "$(uname -s)" in
    # Windows: Visual Studio, 64-bit (a ninja on the PATH may drive MinGW, which cannot build JUCE).
    MINGW* | MSYS* | CYGWIN*) generator=(-A x64) ;;
    *) if command -v ninja > /dev/null; then generator=(-G Ninja); fi ;;
esac
mac=()
if [ "$(uname -s)" = Darwin ]; then
    mac=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_MIN:-11.0}")
fi
# shellcheck disable=SC2086
# (The ${a[@]+...} form: macOS's bash 3.2 calls an empty array unbound under set -u.)
cmake -B "${BUILD_DIR:-build}" ${generator[@]+"${generator[@]}"} -DCMAKE_BUILD_TYPE=Release ${mac[@]+"${mac[@]}"} $CMAKE_ARGS
cmake --build "${BUILD_DIR:-build}" --config Release ${BUILD_TARGET:+--target "$BUILD_TARGET"}
if [ "${RUN_TESTS:-1}" = 1 ]; then
    ctest --test-dir "${BUILD_DIR:-build}" -C Release --output-on-failure --no-tests=ignore
fi
bash packaging/ci/package.sh
if [ "$(uname -s)" = Darwin ]; then
    bash packaging/ci/validate-macos.sh
fi
````

### `plugin-pipeline/files/packaging/ci/common.sh`

````bash
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
````

### `plugin-pipeline/files/packaging/ci/latest-link.sh`

````bash
#!/usr/bin/env bash
# Keeps a fixed download link per branch: a GitHub pre-release "test-<branch>" whose
# <Product>-<platform>.zip is replaced by every build, so one bookmark always gets the
# newest one:
#   https://github.com/<owner>/<repo>/releases/download/test-<branch>/<Product>-macOS.zip
# Runs in GitHub Actions after package.sh (needs GH_TOKEN and contents: write).
source "$(dirname "$0")/common.sh"
[ "${LATEST_LINK:-1}" = 1 ] || { echo "LATEST_LINK is off."; exit 0; }
: "${GITHUB_REF_NAME:?run in GitHub Actions}" "${LATEST_ARCHIVE:?run package.sh first}"

tag="test-$(printf '%s' "$GITHUB_REF_NAME" | sed 's/[^A-Za-z0-9._-]/-/g')"
notes="Newest test build of the $GITHUB_REF_NAME branch (commit ${GITHUB_SHA:0:7}, $(date -u '+%Y-%m-%d %H:%M UTC')). Replaced by every push; unzip and run the installer inside."

# macOS and Windows jobs both get here; whichever is first creates the release.
if ! gh release view "$tag" > /dev/null 2>&1; then
    gh release create "$tag" --prerelease --target "$GITHUB_SHA" \
        --title "Test build: $GITHUB_REF_NAME" --notes "$notes" || true
fi
gh release upload "$tag" "$LATEST_ARCHIVE" --clobber
gh release edit "$tag" --notes "$notes" > /dev/null
# The tag follows the branch, so the release page names the commit it was built from.
git push --force origin "$GITHUB_SHA:refs/tags/$tag" 2> /dev/null || true

url="https://github.com/$GITHUB_REPOSITORY/releases/download/$tag/$(basename "$LATEST_ARCHIVE")"
echo "Fixed link: $url"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    echo "Fixed link (always the newest build of $GITHUB_REF_NAME): $url" >> "$GITHUB_STEP_SUMMARY"
fi
````

### `plugin-pipeline/files/packaging/ci/package.sh`

````bash
#!/usr/bin/env bash
# Packs the built plug-ins into a ready-to-install zip in dist/:
#   macOS    <Product>-macOS-<commit>.zip    AU, VST3, CLAP, app + "Install.command"
#   Windows  <Product>-Windows-<commit>.zip  VST3, CLAP, exe + "Install.bat"
#   Linux    <Product>-Linux-<commit>.zip    VST3, CLAP, standalone + "install.sh"
# Also writes dist/<Product>-<platform>.zip (no commit in the name) for the fixed link, and
# ARTIFACT_NAME / ARCHIVE / LATEST_ARCHIVE to $GITHUB_ENV when run in GitHub Actions.
source "$(dirname "$0")/common.sh"

case "$(uname -s)" in
    Darwin) platform=macOS ;;
    MINGW* | MSYS* | CYGWIN*) platform=Windows ;;
    Linux) platform=Linux ;;
    *) echo "error: unsupported system $(uname -s)" >&2; exit 1 ;;
esac

folder="$safe-$platform-test"
stage="dist/$folder"
rm -rf "$stage"
mkdir -p "$stage"
for b in "${bundles[@]}"; do
    cp -R "$b" "$stage/"
done

# The read-me, with the product's name in it.
render() { sed "s|{{PRODUCT}}|$product|g; s|{{VERSION}}|$version|g" "$1"; }

case "$platform" in
    macOS)
        for b in "$stage"/*.component "$stage"/*.vst3 "$stage"/*.clap "$stage"/*.app; do
            [ -e "$b" ] || continue
            # A complete ad-hoc signature: Apple Silicon only loads signed code, and copying
            # bundles around can leave the linker's signature incomplete. Not a Developer ID
            # signature (Install.command clears the download quarantine instead).
            codesign --force --deep --sign - "$b"
            for exe in "$b"/Contents/MacOS/*; do lipo -info "$exe" || true; done
        done
        cp packaging/macos/install.command "$stage/Install.command"
        chmod +x "$stage/Install.command"
        render packaging/macos/README.txt > "$stage/README.txt"
        archive="$safe-macOS-$version.zip"
        # ditto keeps what Finder needs (permissions, symlinks inside bundles).
        ditto -c -k --keepParent "$stage" "dist/$archive"
        ;;
    Windows)
        # Batch files want CRLF line ends.
        sed 's/$/\r/' packaging/windows/install.bat > "$stage/Install.bat"
        render packaging/windows/README.txt | sed 's/$/\r/' > "$stage/README.txt"
        archive="$safe-Windows-$version.zip"
        (cd dist && 7z a -tzip "$archive" "$folder" > /dev/null)
        ;;
    Linux)
        cp packaging/linux/install.sh "$stage/install.sh"
        chmod +x "$stage/install.sh"
        render packaging/linux/README.txt > "$stage/README.txt"
        archive="$safe-Linux-$version.zip"
        (cd dist && rm -f "$archive" && zip -qry "$archive" "$folder")
        ;;
esac

latest="$safe-$platform.zip"
cp "dist/$archive" "dist/$latest"
echo "Packed dist/$archive:"
for b in "${bundles[@]}"; do echo "  $(basename "$b")"; done

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "ARTIFACT_NAME=$safe-$platform-$version"
        echo "ARCHIVE=dist/$archive"
        echo "LATEST_ARCHIVE=dist/$latest"
    } >> "$GITHUB_ENV"
fi
````

### `plugin-pipeline/files/packaging/ci/validate-macos.sh`

````bash
#!/usr/bin/env bash
# Checks the macOS plug-ins the way hosts will:
#   AU    Apple's auval (what Logic runs before it lists a plug-in). Type, subtype and
#         manufacturer come from the component's own Info.plist.
#   VST3  Tracktion's pluginval, when PLUGINVAL=1 in packaging/pipeline.conf.
# Run after package.sh on a Mac (the CI does). It installs the AU for the current user.
source "$(dirname "$0")/common.sh"

status=0
for comp in "$artefacts"/AU/*.component; do
    [ -e "$comp" ] || continue
    plist="$comp/Contents/Info.plist"
    type="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:type" "$plist")"
    subtype="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:subtype" "$plist")"
    manufacturer="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:manufacturer" "$plist")"
    mkdir -p ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/Components/"$(basename "$comp")"
    cp -R "$comp" ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar 2> /dev/null || true
    echo "auval -v $type $subtype $manufacturer"
    auval -v "$type" "$subtype" "$manufacturer" || status=1
done

if [ "${PLUGINVAL:-0}" = 1 ]; then
    for vst3 in "$artefacts"/VST3/*.vst3; do
        [ -e "$vst3" ] || continue
        tools="$(mktemp -d)"
        curl -fsSL -o "$tools/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip
        ditto -x -k "$tools/pluginval.zip" "$tools"
        "$tools/pluginval.app/Contents/MacOS/pluginval" --strictness-level 5 --skip-gui-tests --validate "$vst3" || status=1
    done
fi

exit "$status"
````

### `plugin-pipeline/files/packaging/linux/README.txt`

````text
{{PRODUCT}} test build for Linux ({{VERSION}}; x86-64)

Run ./install.sh (VST3 to ~/.vst3, CLAP to ~/.clap), then rescan plug-ins in your DAW.
````

### `plugin-pipeline/files/packaging/linux/install.sh`

````bash
#!/usr/bin/env bash
# Installs every plug-in next to this file for the current user.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
shopt -s nullglob
for b in "$here"/*.vst3; do mkdir -p ~/.vst3 && rm -rf ~/.vst3/"$(basename "$b")" && cp -R "$b" ~/.vst3/ && echo "Installed $(basename "$b") -> ~/.vst3"; done
for b in "$here"/*.clap; do mkdir -p ~/.clap && cp -R "$b" ~/.clap/ && echo "Installed $(basename "$b") -> ~/.clap"; done
echo "Restart your DAW and rescan plug-ins."
````

### `plugin-pipeline/files/packaging/macos/README.txt`

````text
{{PRODUCT}} test build for macOS ({{VERSION}}; Apple Silicon and Intel)

1. Unzip this folder (Safari does it for you).
2. Double-click Install.command.
   If macOS says it cannot be opened: right-click it, choose Open, then Open again.
   Or in Terminal:  bash Install.command
3. Restart your DAW.
   - Logic: the instrument or effect menu, under the manufacturer's name
   - Ableton Live, Reaper, Bitwig: rescan plug-ins (VST3 or AU)
   - The standalone app (if included) is in ~/Applications

Installed per user, no admin password:
  AU    ~/Library/Audio/Plug-Ins/Components
  VST3  ~/Library/Audio/Plug-Ins/VST3
  CLAP  ~/Library/Audio/Plug-Ins/CLAP
  App   ~/Applications

Not signed or notarised (that needs an Apple Developer ID); Install.command removes the
download quarantine so macOS lets it load. To uninstall, delete the files above.
````

### `plugin-pipeline/files/packaging/macos/install.command`

````bash
#!/bin/bash
# Installs every plug-in next to this file for the current user (no admin password).
# Double-click it in Finder. This is an unsigned test build: it also removes the download
# quarantine, so macOS lets the plug-ins load.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
xattr -cr "$here" 2> /dev/null || true

installed=()
tilde='~'
put()
{
    mkdir -p "$2"
    rm -rf "$2/$(basename "$1")"
    cp -R "$1" "$2/"
    installed+=("$(basename "$1")  ->  ${2/#$HOME/$tilde}")
}
shopt -s nullglob
for b in "$here"/*.component; do put "$b" "$HOME/Library/Audio/Plug-Ins/Components"; done
for b in "$here"/*.vst3; do put "$b" "$HOME/Library/Audio/Plug-Ins/VST3"; done
for b in "$here"/*.clap; do put "$b" "$HOME/Library/Audio/Plug-Ins/CLAP"; done
for b in "$here"/*.app; do put "$b" "$HOME/Applications"; done

# Make Logic and other AU hosts see the new version.
killall -9 AudioComponentRegistrar 2> /dev/null || true

echo "Installed:"
for line in "${installed[@]}"; do echo "  $line"; done
echo
echo "Restart your DAW. Logic: the plug-in appears under its manufacturer's name."
````

### `plugin-pipeline/files/packaging/pipeline.conf`

````bash
# Settings for the plug-in build pipeline (.github/workflows/plugin-build.yml and
# packaging/ci/*.sh). Plain bash: KEY="value", no spaces around "=".

# Name used for the download (zip and folder). Empty: the plug-in bundle's own name.
PRODUCT=""

# The juce_add_plugin(...) target, used to find its <target>_artefacts folder.
# Empty: the only *_artefacts folder in the build (set it if there are several).
PLUGIN_TARGET="@PLUGIN_TARGET@"

# Extra CMake configure flags, e.g. "-DMY_PROJECT_BUILD_PLUGIN=ON".
CMAKE_ARGS="@CMAKE_ARGS@"

# Build only this target (e.g. "MyPlugin_All": every format of one plug-in). Empty: everything.
BUILD_TARGET=""

# Oldest macOS the plug-in runs on (11.0 is the first with Apple Silicon).
MACOS_MIN="11.0"

# 1: run ctest after the build (a failing test stops the download from being made).
RUN_TESTS=1

# 1: also build a Windows VST3 download.
WINDOWS=1

# 1: keep a fixed download link per branch up to date, as a GitHub pre-release named
#    "test-<branch>" (https://github.com/<you>/<repo>/releases/tag/test-main).
LATEST_LINK=1

# 1: also run Tracktion's pluginval on the VST3 (strictness 5). Stricter than auval; it
#    finds real bugs but may need fixes in the plug-in before it passes.
PLUGINVAL=0
````

### `plugin-pipeline/files/packaging/windows/README.txt`

````text
{{PRODUCT}} test build for Windows ({{VERSION}}; 64-bit)

1. Unzip this folder.
2. Double-click Install.bat and allow it to make changes (VST3 lives in Program Files).
   If Windows SmartScreen warns: More info, then Run anyway (the build is not signed).
3. Restart your DAW and rescan plug-ins.

Installed to:
  VST3  C:\Program Files\Common Files\VST3
  CLAP  C:\Program Files\Common Files\CLAP
The standalone .exe (if included) runs from this folder.
````

### `plugin-pipeline/files/packaging/windows/install.bat`

````bat
@echo off
rem Installs every plug-in next to this file. VST3 and CLAP live in Program Files, so this
rem asks for administrator rights once.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
cd /d "%~dp0"
set "VST3=%CommonProgramFiles%\VST3"
set "CLAP=%CommonProgramFiles%\CLAP"
if not exist "%VST3%" mkdir "%VST3%"
for /d %%B in (*.vst3) do (
    if exist "%VST3%\%%B" rmdir /s /q "%VST3%\%%B"
    xcopy /e /i /q /y "%%B" "%VST3%\%%B\" >nul
    echo Installed %%B  -^>  %VST3%
)
for %%B in (*.clap) do (
    if not exist "%CLAP%" mkdir "%CLAP%"
    copy /y "%%B" "%CLAP%\" >nul
    echo Installed %%B  -^>  %CLAP%
)
echo.
echo Restart your DAW and rescan plug-ins. The standalone .exe runs from this folder.
pause
````

### `plugin-pipeline/new-plugin.sh`

````bash
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
````

### `plugin-pipeline/starter/CMakeLists.txt`

````cmake
cmake_minimum_required(VERSION 3.22)

# Before project(): the oldest macOS the plug-in runs on (11.0: the first with Apple Silicon).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(@TARGET@ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
endif()

# JUCE is downloaded at a fixed version, so a fresh machine (or the CI) needs nothing
# installed. Offline: cmake -B build -DJUCE_DIR=/path/to/JUCE
set(JUCE_TAG "8.0.9" CACHE STRING "JUCE git tag")
set(JUCE_DIR "" CACHE PATH "Local JUCE checkout (skips the download)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# The plug-in's identity. Keep the codes once released: hosts recognise plug-ins (and
# their saved sessions) by them. PLUGIN_MANUFACTURER_CODE is yours, the same for all your
# plug-ins (4 characters, at least one capital); PLUGIN_CODE is unique per plug-in.
juce_add_plugin(@TARGET@
    PRODUCT_NAME "@PRODUCT@"
    COMPANY_NAME "@COMPANY@"
    BUNDLE_ID "@BUNDLE_ID@"
    PLUGIN_MANUFACTURER_CODE @MANUFACTURER_CODE@
    PLUGIN_CODE @PLUGIN_CODE@
    FORMATS AU VST3 Standalone
    IS_SYNTH @IS_SYNTH@
    NEEDS_MIDI_INPUT @IS_SYNTH@
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES @VST3_CATEGORY@
    AU_MAIN_TYPE @AU_MAIN_TYPE@)

target_sources(@TARGET@ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(@TARGET@ PRIVATE Source)

target_compile_definitions(@TARGET@ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(@TARGET@
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless tests of the processor (no DAW, no window): run by ctest locally and in CI.
option(BUILD_TESTS "Build the plug-in tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(@TARGET@_Tests tests/PluginTests.cpp)
    target_include_directories(@TARGET@_Tests PRIVATE
        Source
        $<TARGET_PROPERTY:@TARGET@,INCLUDE_DIRECTORIES>)
    target_compile_definitions(@TARGET@_Tests PRIVATE
        $<TARGET_PROPERTY:@TARGET@,COMPILE_DEFINITIONS>)
    target_link_libraries(@TARGET@_Tests PRIVATE @TARGET@)
    add_test(NAME processor COMMAND @TARGET@_Tests)
endif()
````

### `plugin-pipeline/starter/Source/PluginEditor.cpp`

````cpp
#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p),
      gainAttachment (p.parameters, "gain", gain)
{
    gain.setTextValueSuffix (" dB");
    addAndMakeVisible (gain);
    setSize (320, 240);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1d1f24));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText (JucePlugin_Name, getLocalBounds().removeFromTop (48), juce::Justification::centred);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("GAIN", getLocalBounds().removeFromBottom (28), juce::Justification::centred);
}

void PluginEditor::resized()
{
    gain.setBounds (getLocalBounds().reduced (60, 48));
}
````

### `plugin-pipeline/starter/Source/PluginEditor.h`

````cpp
#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** The plug-in's name and one GAIN knob. */
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider gain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
````

### `plugin-pipeline/starter/Source/PluginProcessor.cpp`

````cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct SineSound final : juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    /** A sine with a short envelope: enough to hear that the plug-in loads and plays. */
    struct SineVoice final : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SineSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            step = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
            level = 0.25f * velocity;
            envelope.setSampleRate (getSampleRate());
            envelope.setParameters ({ 0.005f, 0.2f, 0.7f, 0.3f });
            envelope.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
                envelope.noteOff();
            else
            {
                envelope.reset();
                clearCurrentNote();
            }
        }

        using SynthesiserVoice::renderNextBlock;   // (the double-precision one stays as is)

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int count) override
        {
            if (! isVoiceActive())
                return;
            for (int i = start; i < start + count; ++i)
            {
                const auto value = static_cast<float> (std::sin (phase)) * level * envelope.getNextSample();
                phase += step;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, i, value);
            }
            if (! envelope.isActive())
                clearCurrentNote();
        }

        double phase = 0.0, step = 0.0;
        float level = 0.0f;
        juce::ADSR envelope;
    };
}

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsSynth
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
#endif
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STATE", createLayout())
{
    gainDb = parameters.getRawParameterValue ("gain");
    for (int i = 0; i < 8; ++i)
        synth.addVoice (new SineVoice());
    synth.addSound (new SineSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain",
                                                             juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != out)
        return false;
#endif
    return true;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

#if JucePlugin_IsSynth
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
#else
    juce::ignoreUnused (midi);
#endif

    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto g = gain.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
````

### `plugin-pipeline/starter/Source/PluginProcessor.h`

````cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    The starting point: one GAIN parameter. Built as an instrument (IS_SYNTH TRUE) it plays
    a simple eight-voice sine synth; as an effect it applies the gain to its input.
    Replace the DSP, keep the shape: parameters in the value tree, state saved as XML,
    nothing allocated in processBlock().
*/
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::Synthesiser synth;
    juce::SmoothedValue<float> gain;
    std::atomic<float>* gainDb = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
````

### `plugin-pipeline/starter/gitignore`

````text
/build*/
/cmake-build-*/
/dist/
.DS_Store
*.user
.vs/
.idea/
.vscode/
````

### `plugin-pipeline/starter/tests/PluginTests.cpp`

````cpp
// Headless checks of the processor, run by ctest (locally and in CI before anything is
// packed): it loads, makes sound (instrument) or passes sound (effect), stays finite, and
// its state comes back. Add a check here for every bug you fix.
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    float peakOf (const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            peak = std::max (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
        return peak;
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        PluginProcessor processor;
        const int block = 256;
        processor.setPlayConfigDetails (processor.getTotalNumInputChannels(), 2, rate, block);
        processor.prepareToPlay (rate, block);

        juce::AudioBuffer<float> buffer (std::max (2, processor.getTotalNumInputChannels()), block);
        float loudest = 0.0f;
        bool allFinite = true;
        for (int n = 0; n < 40; ++n)
        {
            juce::MidiBuffer midi;
            if (n == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            // An effect gets a test tone in; an instrument makes its own.
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, 0.25f * std::sin (0.05f * static_cast<float> (n * block + i)));
            processor.processBlock (buffer, midi);
            loudest = std::max (loudest, peakOf (buffer));
            allFinite = allFinite && finite (buffer);
        }
        std::printf ("-- %.0f Hz: peak %.3f\n", rate, loudest);
        check (loudest > 1.0e-3f, "makes sound");
        check (loudest < 2.0f, "stays below +6 dBFS");
        check (allFinite, "no NaN or infinity");
        processor.releaseResources();
    }

    // State: a changed parameter comes back in a fresh instance.
    {
        PluginProcessor a;
        a.parameters.getParameter ("gain")->setValueNotifyingHost (0.25f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        PluginProcessor b;
        b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (std::abs (b.parameters.getParameter ("gain")->getValue() - 0.25f) < 1.0e-4f, "state is recalled");
    }

    // The editor opens and closes (no host needed; skipped without a display, e.g. on a
    // headless Linux machine).
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 0, "editor opens");
    }

    std::printf (failures == 0 ? "all checks passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
````

