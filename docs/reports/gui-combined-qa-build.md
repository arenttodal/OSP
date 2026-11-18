# Combined macOS test build: macro D2 + layer-ivory B (Phase 5)

**What it is:** one install for the manual tests of both knob families.
- The six macros with the D2 cap ([arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26)).
- The five source knobs with layer-ivory B ([arenttodal/OSP#28](https://github.com/arenttodal/OSP/pull/28), stacked on #26).

**Status:** every box in the checklists is **open**. Nothing has been tested on a real Retina Mac or in a
DAW yet. Tick a box only after the check has really been done.

## The build

| | |
|---|---|
| Commit | `f58788e` (`claude/gui-layer-ivory`, PR #28 head) = `3354690` (D2) + layer-ivory B |
| CI run | [44303244794](https://github.com/arenttodal/OSP/actions/runs/44303244794), `build-test (macos-15, plugin ON)` |
| Artifact | **`OSP-macOS-test`** (id 14431031754) |
| Contents | `OSP.component` (AU), `OSP.vst3`, `OSP.app`, `install.command`, README. Universal binary. Not signed or notarised |

**Install and rollback** work as in `knob-d2-manual-qa.md`, sections 2–3:
- **Install:** `install.command` replaces the AU, the VST3 and the app.
- **Roll back to D2 only:** the PR #26 build (run 44153563550).
- **Roll back to shipping:** `56bab47` (run 44089223844).

The packs change only images, so projects and presets open in every build.

## What to test

**Macros (D2):** the full checklist in `knob-d2-manual-qa.md` section 4.

**Source knobs (layer-ivory B):** START, TUNE, PAN, LEVEL and REIMAGINED on cards A, B and C.

- [ ] Slow and fast drags end to end: cap, pointer and arc agree, with no stepping.
- [ ] Fine drag (modifier or scroll wheel): every step moves the cap.
- [ ] Double-click reset: the cap matches the arc.
- [ ] Modulation on a source knob (e.g. LFO → TUNE): rings draw over the cap while it runs.
- [ ] Editor resizing through the full range: caps stay sharp, nothing is cropped, the shadows don't step.
- [ ] One and three layers: the cards' knob rows look the same in both.
- [ ] Preset switching and editor reopening (about ten times): correct on the first paint.
- [ ] **Retina and a 1× display:** drag the window between them. The caps re-render sharp on each.
- [ ] **Logic (AU) and Live (VST3):** automate a source knob with the editor open and closed; run four or more instances with editors open.

**Both families together:**
- [ ] Side by side at 100 % on Retina: the macros and the source knobs read as one family (ivory tone,
      light direction, shadow strength).
- [ ] CPU with eight instances and all editors open: no audio dropouts. Note the DAW's CPU meter.

## What counts as a fail

Visible stepping, a cap that disagrees with its arc, blurred caps after a resize or a display change,
a wrong first frame, or dropouts. Note the host, the display and the steps.
