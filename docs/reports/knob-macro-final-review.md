# Macro knob: final review (Package B, 17 Nov 2026)

Candidate C (production height, `studio-sculpt`) was approved as the direction. This round reduces only its contact
shadow. Everything was measured in the **current OSP editor** (default branch `08ebd01`, built in a detached worktree):
- A = the shipping sprites;
- C = the gate 1 candidate;
- D2 = this round's recommendation.

## D candidates: shadow only

`postprocess.shadow_trim` (tools/blender-assets):
- **Why it can be exact:** the camera looks straight down at a round knob, so its silhouette is a known circle (14 mm body radius + 0.45 mm skirt).
- **What it changes:** outside that circle, every pixel is the shadow catcher's (black, alpha = density). Alpha there is scaled to `strength`, after an untouched 0.4 mm contact band and a 1 mm smooth ramp.
- **What it leaves alone:** pixels inside the silhouette (the cap, flutes, shoulder, sidewall, indicator) never change. So the cap's mid-tone, calibration and pointer contrast are C's exactly, with no re-render and no alpha change on the knob.
- **Default:** strength 1 is a no-op, and existing packs stay byte-identical.

Measured alpha along the cast direction (master frame, mid position):

| mm from the centre | 14.75 (contact) | 15.0 | 15.5 | 16.0 | 17.0 | 18.0 |
|---|---|---|---|---|---|---|
| C | 173 | 166 | 160 | 157 | 150 | 102 |
| D1 (strength 0.9) | 173 | 165 | 149 | 141 | 135 | 91 |
| **D2 (strength 0.8)** | 173 | 164 | 139 | 125 | 120 | 81 |

**Recommendation: D2.** The shadow reads as anchored, not as a detached drop shadow. The fluting, shoulder and sidewall are C's. At actual 1× size there is still a clear lower-right shadow.

## Full rotation (D2)

| Check | Result |
|---|---|
| Frames | All 129 frames present: 512 px masters, RGBA, one size |
| Pointer accuracy | `verify.py`: largest indicator error 0.21° over all 129 frames |
| Start and end angles | Taken from the manifest (unchanged format; a `shadow` entry records the trim) |
| Temporal consistency | Shadow mass in the cast quadrant varies 0.4 % over the turn (largest frame-to-frame step 0.05/255). The cap, in a sector the pointer never crosses, varies 0.15/255 (largest step 0.024/255). No flicker |
| In the real editor | `[sprite-knob]` 2053 assertions pass (frame order against the drawn angles, pivot, caches, skins, fallbacks), for A, C and D2 alike |

## Real-editor comparisons (`knob-final-review/`)

- `04-window-{869,1086,1810}_macros.jpg`: minimum, default and maximum window sizes.
- `03-three-layers@{1.5x,2.0x}_macros.jpg`: HiDPI and Retina.
- `full_1086_2x.jpg`, `full_1448_1x.jpg`: the full instrument.
- `life_values_2x.jpg`: LIFE at min, 25 %, default, 75 % and max. The pointer meets the arc's end every time.
- `life_modulation_2x.jpg`: one route, several routes, and meta modulation. The arcs and halos are vector and unchanged.
- Gate 1 sheets that were missing from the earlier ZIP: `compare_176.jpg`, `compare_352.jpg` (A / B / C frames), and `d_study_176.jpg`, `d_study_352.jpg`, `d_shadow_zoom.jpg` (A / C / D1 / D2).
- **Interaction videos:** `turn_A.mp4`, `turn_C.mp4`, `turn_D2.mp4`, `turn_A_vs_D2.mp4` and `resize_D2.mp4`.
  - Contents:
    - a slow sweep (4 s end to end);
    - a fast one (0.4 s);
    - a fine adjustment (3 % in 40 steps);
    - sine automation;
    - live LFO modulation on LIFE and SPACE, with audio processed between frames;
    - a resize from 869 to 1810 px.
  - Rendered frame by frame at 2× by the real editor code, at 30 fps.
  - These are headless renders. **A slow drag on a real Retina display remains to be judged by eye.**

## Performance (current build, idle CPU, sequential runs)

| | A | C | D2 |
|---|---|---|---|
| 176 px pack | 2.69 MB | 2.70 MB | 2.73 MB |
| 352 px pack | 8.60 MB | 8.88 MB | 9.00 MB |
| LIFE, warm paint at 1× / 2× (median) | 0.22 / 0.22 ms | 0.24 / 0.36 ms | 0.25 / 0.24 ms |
| LIFE, first show of each frame at 2× (median) | 3.8 ms | 4.0 ms | 3.9 ms |
| Vector knob paint at 2× (median, reference) | 2.2 ms | 2.2 ms | 2.3 ms |
| All 129 frames decoded, 1× / 2× | 9.2 / 36.7 MB | same | same |
| Editor open + first 2× paint, cold / warm | 483 / 399 ms | 498 / 421 ms | 555 / 399 ms |
| 4 instances, editors open, painted at 2× | +120 MB | +120 MB | +120 MB |
| Sweep of 6 macros × 4 editors at 2× (3096 knob paints) | 1.18 s | 1.03 s | 1.07 s |
| 20 open / close cycles | +8.2 MB | +8.2 MB | +8.2 MB |

**Reading the numbers:**
- **Plugin size:** D2 adds 0.44 MB of embedded data against A (the packs are stored, not recompressed).
- **No measurable difference:** decode memory and paint cost depend only on frame size and count, which are unchanged, and the timings agree within run-to-run noise.
- **What "whole editor at 2×" measures:** 70-77 ms median, the same for all three. That is a full software snapshot of the editor (`createComponentSnapshot`), a full CPU repaint, **not a frame rate**: a host repaints only dirty regions, through CoreGraphics on macOS. The gate 1 figure of 111 ms came from the older branch build and its taller editor, so it is not comparable.
- **For a separate look (not caused by the sprites):** process RSS grows 175-320 MB over 63 full 2× snapshots at changing sizes, in all three variants alike.
  - The snapshot images themselves are up to 3620 × 2466 px.
  - So this is most likely allocator retention plus per-size image caches.

## 129 or 257 frames

Keep 129. The pointer error is 0.21° and nothing steps in the stills or the rendered sequence. 257 frames would double both packs and the decoded memory (73 MB at 2× with every frame shown). Revisit only if a slow drag on a Retina Mac shows stepping.

## To ship D2

- **Files:** replace the two packs (`shared/andor-ui/Resources/knobs/macro-ivory/`) and set `"shadow"` in `tools/blender-assets/config/macro-knob.json`.
- **Code:** no change; the manifest format is the same.
- **Where:** on the OSP default line. See the shared system audit (Package C) for the branch question.
