# ANDOR Lab — builds, artefacts and validation

No installers, no signing, no notarisation, no copying into system plug-in folders: these are
development builds. The installer is a later release step.

## Targets (actual CMake names)

| Product | Shared code | VST3 | AU (macOS) | Standalone | Plug-in tests |
| --- | --- | --- | --- | --- | --- |
| ANDOR Phrase | `ANDOR_Phrase` | `ANDOR_Phrase_VST3` | `ANDOR_Phrase_AU` | `ANDOR_Phrase_Standalone` | `andor_phrase_plugin_tests` (ctest `andor-phrase-plugin`) |
| ANDOR Grain | `ANDOR_Grain` | `ANDOR_Grain_VST3` | `ANDOR_Grain_AU` | `ANDOR_Grain_Standalone` | `andor_grain_plugin_tests` (ctest `andor-grain-plugin`) |

Engine/analysis/atlas tests (no plug-in, no GUI): `andor_tests` with ctest entries
`andor-atlas`, `andor-phrase`, `andor-grain`, `andor-interop`, `andor-realtime`.
Every plug-in target has `COPY_PLUGIN_AFTER_BUILD FALSE`.

Identities (verified unique against ANDOR/OSP `Osp1` and ANDOR Drums `Adrm`, on the
`drum-plugin` branch):

| | Manufacturer | Plug-in code | Bundle id | State tag | Presets |
| --- | --- | --- | --- | --- | --- |
| ANDOR Phrase | `Ospx` | `Aphr` | `com.osp.andorphrase` | `ANDOR_PHRASE_STATE` | `Documents/ANDOR Lab/Phrase Presets/*.andorphrase` |
| ANDOR Grain | `Ospx` | `Agrn` | `com.osp.andorgrain` | `ANDOR_GRAIN_STATE` | `Documents/ANDOR Lab/Grain Presets/*.andorgrain` |

Both share the managed sample store `<user app data>/ANDOR Lab/Samples` (one copy of a sample
on disk, content-addressed); OSP keeps its own store.

## Commands

The repository's established workflow (Ninja; Xcode works the same way on a Mac):

```sh
# Headless: engines, analysis, Note Atlas, CLI, all OSP and ANDOR Lab engine tests
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# Plug-ins (OSP, ANDOR Phrase, ANDOR Grain) and their headless processor/editor tests
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON          # -DANDOR_BUILD_LAB=OFF builds OSP only
cmake --build build-plugin --target ANDOR_Phrase_All ANDOR_Grain_All andor_phrase_plugin_tests andor_grain_plugin_tests
ctest --test-dir build-plugin -R andor --output-on-failure    # on Linux under xvfb-run

# macOS universal, Release, Xcode (representative)
cmake -S . -B build/andor-lab -G Xcode -DOSP_BUILD_PLUGIN=ON "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build/andor-lab --config Release --target ANDOR_Phrase_All ANDOR_Grain_All
ctest --test-dir build/andor-lab -C Release -R andor --output-on-failure
```

Linux plug-in builds need: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev
libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev` (and `xvfb` to run
editor tests headless).

## Artefacts (this container: Linux x86-64, Release)

```
build-plugin/apps/phrase/ANDOR_Phrase_artefacts/Release/VST3/ANDOR Phrase.vst3
build-plugin/apps/phrase/ANDOR_Phrase_artefacts/Release/Standalone/ANDOR Phrase
build-plugin/apps/grain/ANDOR_Grain_artefacts/Release/VST3/ANDOR Grain.vst3
build-plugin/apps/grain/ANDOR_Grain_artefacts/Release/Standalone/ANDOR Grain
```

On macOS the same folders hold `ANDOR Phrase.component` / `ANDOR Grain.component` (AU) and
`.app` bundles. **No macOS build was made in this environment.**

## Validation results

| Check | Environment | Result |
| --- | --- | --- |
| `andor_tests` (engines, analysis, atlas, interop, real-time allocation) | Linux container | pass |
| `andor_phrase_plugin_tests` (identity, load/analyse/play, recall bit-identical, missing source + relink, cache, undo/redo, presets, export, host rates 22.05–192 kHz × blocks 1–4096, edits during playback, `processBlock` allocation-free, editor) | Linux + Xvfb | pass |
| `andor_grain_plugin_tests` (identity, sample, KEY DNA on the audio thread, seeds, oscillator, modulation, recall/presets bit-identical, Note Atlas interop, missing atlas audio, host rates × blocks, `processBlock` allocation-free, editor) | Linux + Xvfb | pass |
| **pluginval 1.0.4, strictness 10**, `--validate-in-process`, VST3 | Linux + Xvfb | **SUCCESS** for both (logs: `data/pluginval-ANDOR-Phrase.txt`, `data/pluginval-ANDOR-Grain.txt`). Its embedded Steinberg `validator` step needs a separately built validator binary and was not run. |
| Standalone start (6 s) and SIGTERM shutdown | Linux + Xvfb, no audio device | both start and exit cleanly |
| OSP's own suites after all changes (`unit`, `integration`, `regression`, `performance-smoke`, `plugin`) | Linux | see `handoff.md` §4 |
| AU validation (`auval -v aumu Aphr Ospx`, `auval -v aumu Agrn Ospx`) | needs macOS | **pending** |
| Ableton Live and a second host (Logic / Reaper) | needs the user's Mac | **pending** — `test-instructions.md` |

AU and host results must not be inferred from these Linux runs.

## Development-only installation on a Mac (explicit action, never automatic)

After a macOS build, copy (do not move — keep the build artefacts for debugging):

```sh
cp -R "build/andor-lab/apps/phrase/ANDOR_Phrase_artefacts/Release/VST3/ANDOR Phrase.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "build/andor-lab/apps/phrase/ANDOR_Phrase_artefacts/Release/AU/ANDOR Phrase.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "build/andor-lab/apps/grain/ANDOR_Grain_artefacts/Release/VST3/ANDOR Grain.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "build/andor-lab/apps/grain/ANDOR_Grain_artefacts/Release/AU/ANDOR Grain.component" ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumu Aphr Ospx && auval -v aumu Agrn Ospx
```

The unique codes and bundle ids mean these never replace an installed ANDOR/OSP or ANDOR
Drums. To remove: delete the four bundles. Unsigned builds may need
`xattr -dr com.apple.quarantine <bundle>` when copied from another machine.
