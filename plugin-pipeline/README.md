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
| macOS: universal build, tests, ad-hoc signing, packaging, `auval` | Pass on GitHub's macOS 14 runners: OSP's `plugin-pipeline-selftest` workflow builds a starter plugin through `build-local.sh` whenever the kit changes |
| Windows: Visual Studio x64 build, tests, packaging (`7z`, `Install.bat` with CRLF) | Pass on GitHub's Windows runners (same self-test) |
| `Install.command` on a Mac, `Install.bat` on a PC, pluginval, the fixed link | Not run yet. The installers run when you double-click them; pluginval (off by default) and the fixed link run on the first push of a repository using the pipeline |
