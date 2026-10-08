#!/usr/bin/env bash
# Writes HANDOFF.md: the whole kit in one Markdown file (instructions for an AI coding agent
# plus every file's exact contents), to give to another chat or project. Run after changing
# the kit:  bash plugin-pipeline/make-handoff.sh
set -euo pipefail
kit="$(cd "$(dirname "$0")" && pwd)"
out="$kit/HANDOFF.md"

{
cat <<'EOF'
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

EOF

# Every file except this generator and its output, with its exact contents.
cd "$kit/.."
find plugin-pipeline -type f ! -name HANDOFF.md ! -name make-handoff.sh | LC_ALL=C sort | while IFS= read -r f; do
    case "$f" in
        *.md) lang=markdown ;;
        *.yml) lang=yaml ;;
        *.sh | *.command | */pipeline.conf) lang=bash ;;
        *.bat) lang=bat ;;
        *.cpp | *.h) lang=cpp ;;
        */CMakeLists.txt) lang=cmake ;;
        *) lang=text ;;
    esac
    printf '### `%s`\n\n````%s\n' "$f" "$lang"
    cat "$f"
    # Close the fence on its own line even if the file has no final newline.
    [ -z "$(tail -c 1 "$f")" ] || printf '\n'
    printf '````\n\n'
done
} > "$out"
echo "Wrote $out ($(wc -l < "$out") lines)"
