# Macro knob D2 · Manual QA on a real Retina Mac and in DAWs

**What is being tested:** [arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26), the macro sprite upgrade only.

**Status:**
- All boxes below are **open**.
- Nothing has been tested on real hardware or in a live host yet.
- Do not tick a box until that check has actually been done.

## 1. The test build: the exact PR head

| | |
|---|---|
| Commit | `3354690` (`claude/gui-macro-knob-d2`) |
| CI run | [CI run 44153563550](https://github.com/arenttodal/OSP/actions/runs/44153563550): `build-test (macos-15, plugin ON)`, passed. It includes `auval` for the AU |
| Artifact | **`OSP-macOS-test`** (249 MB zip, sha256 `d2abe687…e7b7`, kept until 15 Feb 2027) |
| Contents | `OSP.component` (AU), `OSP.vst3`, `OSP.app` (standalone), ANDOR Memo Lab, `install.command`, README. Universal binary (Apple Silicon and Intel). Not signed or notarised |

**Download:**
1. Open the CI run page while signed in to GitHub.
2. Under **Artifacts**, click **OSP-macOS-test**.
3. Unzip it twice: the artifact zip contains `OSP-macOS-test.zip`.

## 2. Install

1. **Quit every DAW.**
2. **Double-click `install.command`.** If macOS blocks it: right-click, then Open, then Open. Or run `bash install.command` in Terminal.
   - It replaces `~/Library/Audio/Plug-Ins/Components/OSP.component` and `~/Library/Audio/Plug-Ins/VST3/OSP.vst3`, and `~/Applications/OSP.app`.
   - It clears the download quarantine and restarts the AU registrar.
3. **Reopen the DAW:**
   - **Logic:** Instrument slot → AU Instruments → OSP.
   - **Live:** Preferences → Plug-ins → Rescan.
4. **Check the build:** the six macros should show the D2 cap, with a sharper flute and bevel and a lighter cast shadow than before. If they look unchanged, the old copy is still loaded: quit the DAW, run `killall -9 AudioComponentRegistrar` in Terminal, and reopen.

## 3. Rollback (to the shipping line, before D2)

The rollback build is the default branch at **`56bab47`**, the PR's own base and identical apart from the macro packs.

1. Download it from [CI run 44089223844](https://github.com/arenttodal/OSP/actions/runs/44089223844) → **OSP-macOS-test**.
2. Run its `install.command`. It replaces D2 in place.
3. Restart the DAW.

**Remove the build completely:** delete `OSP.component` (Components), `OSP.vst3` (VST3) and `OSP.app` (~/Applications).

**Projects and presets:** D2 changes only images. Projects and presets saved with either build open in the other.

## 4. What to test

**Use:** a Retina display at its default scaled resolution. If one is available, also test on a non-Retina external display, and drag the window between the two.

### Interaction (standalone first, then each host)

- [ ] **Slow drag** on each of the six macros, end to end. The pointer, arc and cap turn together, with no visible stepping or flicker.
- [ ] **Fast drag:** flick end to end. No lag between the cap and its arc, and no blank or wrong frame.
- [ ] **Fine adjustment:** a modifier-drag or the scroll wheel in small steps. Every step moves the cap, with no dead zones.
- [ ] **Double-click reset:** returns to the default, and the cap matches the arc.
- [ ] **Multiple modulation assignments:** assign two or three modulation sources to one macro. Modulation rings and halos draw over the cap correctly while modulation runs.
- [ ] **Editor resizing:** drag the corner through the full range. Caps stay sharp at every size, nothing is cropped, and the shadows don't step.
- [ ] **Preset switching:** step through presets quickly. Caps jump to new values with no stale frames.
- [ ] **Editor reopening:** close and reopen the editor about ten times. It opens without delay, and the caps are correct on the first paint.

### Logic Pro (AU)

- [ ] Loads and validates (Plug-in Manager shows OSP as successfully validated).
- [ ] Automation write on two macros, then playback with the editor open: caps follow smoothly.
- [ ] Automation playback with the editor closed, then reopened mid-playback: caps show the current value at once.
- [ ] **Multiple instances:** four or more OSP tracks with all editors open. Caps are correct in each, and audio has no dropouts.
- [ ] Project save, close and reopen: macros restore and caps match.

### Ableton Live (VST3, and AU where supported)

- [ ] VST3: everything in the Logic list, including automation and several instances.
- [ ] AU in Live (if enabled): load, automate, and run several instances.
- [ ] Live's own plug-in window resizing and "Auto-hide plug-in windows" switching.

### What counts as a fail

Any of these is a fail. Note the host, the display and the steps:
- visible stepping;
- a cap that disagrees with its arc;
- a blank, black or stale cap;
- an unsharp cap at a size;
- a slow first open;
- added CPU when idle.

## 5. Results

| Check | Host / display | Result | Date | Notes |
|---|---|---|---|---|
| | | | | |
