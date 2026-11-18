# GUI assets · Phase 4 (Package B), 18 Nov 2026

Sheets and captures are in `gui-assets-phase4/`.

## 1. Macro D2: manual-QA build

- **Integration vehicle:** PR [arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26), still limited to the macro sprite upgrade.
- **Test build:** the CI artifact **`OSP-macOS-test`** from the exact PR head `3354690`: AU, VST3 and Standalone, universal, passed `auval`.
- **Instructions:** download, install, rollback to the PR base `56bab47`, and the full checklist. They cover slow and fast drags, fine adjustment, automation, several modulation assignments, resize, preset switching, reopening, several instances, Logic AU and Live VST3/AU. They are in `docs/reports/knob-d2-manual-qa.md`.
- **Status:** every manual check is **open**. Nothing has been tested on a real Retina Mac or in a host. The headless sequences do not replace that.

## 2. `layer-ivory` B: the complete pack and the real editor

**The turn:**
- 129 frames;
- the largest pointer error is **0.41°** (`verify.py`);
- packs: `layer-ivory-100.zip` (963 KB) and `layer-ivory-200.zip` (2.9 MB).

**Locked geometry and finish** (`tools/blender-assets/config/layer-knob.json`):
- 20 mm cap, 7 mm tall (H/D 0.35);
- smooth sidewall, domed top, fine skirt, 1 mm bevel;
- the current knob's short pointer;
- `studio-sculpt` light;
- cast shadow 55 % (0.3 mm contact band, 0.8 mm ramp).

**Runtime levels, 100 and 200 px, validated:**

| | Body | Frame needed (×1.45 margin) | Level used |
|---|---|---|---|
| 1448 px window | 52 px | 76 px at 1× | 100 |
| 1810 px window, 2× (the largest supported) | 131 px | 190 px | 200 |

Each level is within 2× of what it is drawn at, so it is never upscaled and never decimated past 2:1.

**Integration branch `claude/gui-layer-ivory`:**
- **Stacking:** on top of `claude/gui-macro-knob-d2`, because it uses D2's light and shadow trim. It moves to the default branch once #26 merges.
- **Wiring:** `LayerKnob`'s dial gets `"sprite": "layer-ivory"`, in both constructors: START, TUNE, PAN, LEVEL and REIMAGINED, on every card.
- **Pack registration:** `AndorUi.cmake` registers the two packs.
- **Comment:** the `KnobSkin::macros` comment is updated.
- **Unchanged:** the renderer, parameters, values, labels, arcs, ticks, halos, REIMAGINED's spectral arc, hit areas and modulation.
- **Rollback:** the `original` skin still shows every vector knob.
- **Product-specific controls:** ANALOG's card row uses the same shared `LayerKnob`, so it shows the same asset when its pin moves (see Package C). No product-specific copy is needed.

**Tests on the branch (Linux, Release):**
- `[sprite-knob]`: 11 cases and 3503 assertions pass, including two new ones:
  - the `layer-ivory` pack (129 frames, levels 100/200, turn direction, pointer error under 3°);
  - "the source cards' five knobs show layer-ivory, sharing one asset".
- `osp_visual_reference`, `andor_example_tests` and `andor_example_tests_visual` pass. The shell product's parity is at 0 px.
- The full `plugin` group passes (2227 s).

**PR:** [arenttodal/OSP#28](https://github.com/arenttodal/OSP/pull/28), stacked on #26; retarget it to the default branch once #26 merges. It still needs the same manual Retina Mac and DAW pass as D2.

**Real-editor captures:** `layer_editor_*`, rendered against original, at 1086, 1448 and 1810 px, 1× and 2×.

## 3. `mini-ivory` pilot

The full 129-frame turn is rendered from the approved starting geometry:
- 12 mm cap, 4.8 mm tall, shallow dome;
- smooth sidewall, no skirt, 0.6 mm bevel;
- a wide pointer;
- cast shadow 50 %.

**Pilot result (`mini_pilot_*`):**
- **ADSR, LFO panel and FX rack cells:** at 1448 × 2 they read cleanly, with pointers on their arcs.
- **The small popover at 1× in the smallest window** (about 12–16 px bodies, `mini_pilot_popover_869_1x_zoom3.png`): the rendered pointer is thinner and lighter than the vector knob's. **Before any roll-out**, the indicator should be wider and darker (width 1.0 → about 1.3 mm, a darker ink), then this site checked again.

**The pilot is wired locally, for review only, at four sites:**
- the ADSR envelope;
- one modulation panel;
- one rack (the FX rack's cells);
- one small popover.

The roughly 150 call sites are untouched.

**Runtime levels (proposed 48 / 96 / 180), checked against physical need** (body radius in design px; the largest window is 1810 at 2×, the smallest 869 at 1×):

| Where | Body r | Largest frame needed | Smallest | Level |
|---|---|---|---|---|
| Rack cells (`setRackMetrics`) | 24.6 | 178 px | 30 px | 180 |
| ADSR / modulation panel | 20 | 145 px | 24 px | 180, or 96 at 1× |
| Popover cells | 13 | 94 px | 16 px | 96 |
| Popover second row (`setSmall`) | 9.6 | 70 px | 12 px | 96 (2×) / 48 (1×) |

**Verdict:** 48 / 96 / 180 is justified. Each size is served from a level at most 2× larger, and nothing is generated bigger than the 178 px maximum.

## 4. `mini-graphite`: G0 / G1 / G2

The sheets (`mini_graphite_G0_G1_G2_1x.png` and `_2x.png`) show bodies of 12, 19, 26 and 40 px over the dark display, at 25 % and 75 %.

All three use the corrected graphite calibration (target #4a4843, G0's exposure for all three). Measured on the 384 px masters, and at 12 px against the display (luminance 29):

| | Treatment | Cap centre | Rim | 12 px outer ring |
|---|---|---|---|---|
| G0 | current | 82.9 | 86.1 | 57.1 |
| **G1 (recommended)** | material a touch lighter over the outer 0.9 mm and the sidewall (`satin-graphite-g1`, rim #57554f) | **82.9 (unchanged)** | 97.8 | **66.0** |
| G2 | stronger, wider edge light plus a second, opposite edge (`studio-sculpt-edge`) | 87.1 (+5 %: lightens the cap) | 94.9 | 62.5 |

**Why G1:**
- It outlines the cap against the waveform display at 12 px without lifting the cap itself.
- G2 brightens the whole cap, which the brief rules out.
- No chrome, gloss or extra rings; the light pointer is kept.
- **Note:** the rim option is opt-in in the knob material (`rim`), so no existing pack changes.

## 5. Mini knob inventory: what needs its own treatment

**One `MiniKnob` class** (ShapingPopups.h) serves all the small rotaries, with opt-in modes:

| Mode | Uses | Own treatment? |
|---|---|---|
| `setArcColour` (the macro's colour on its popover or rack) | 14 | No: the arc stays vector over the shared cap |
| `setRackMetrics` (59 px rack cell) | 4 | No: the same cap, at the 180 level |
| `setSmall` (popover second row) | 3 | No: the same cap, 96/48 levels. Judge at 12 px |
| `setDetents` (stepped rates) | 3 | No: detent marks stay vector |
| `setChoiceOrder` (ARP RATE) | 3 | No: behaviour only |
| `setQuiet` | 2 | No: caption and value only |
| `setOnDark` (over a graphite display) | 1 | **Yes: `mini-graphite`** |
| `setBoxed` (SPACE's DECAY: ticked knob, value box) | 1 | **Keep as is.** Its look is intentionally different |
| `onCaptionClick` (RATE: Sync/Hz) | 1 | No |

**Separate families, not `MiniKnob`:**
- `RackKnob` (RackControls.h);
- the graphite well knobs (EngineCard: GRANULAR / ANALOG wells), which take `mini-graphite`.

**So:** two assets (`mini-ivory`, `mini-graphite`) cover every small rotary, with the boxed DECAY knob left as it is.

## 6. Buttons and touch strip (exploratory, not integrated)

The study uses `tools/blender-assets/skin/button_study.py`, with the ceramic skin's own studio, materials and calibrated exposure, fixed orthographic cameras, transparent backgrounds and real GUI sizes.

**Keys:**
- **Roles:** icon (EQ-sized), push, modifier and ARP.
- **States:** normal, hover, pressed and active.
- **Variants:**
  - **K0:** the current skin key;
  - **K1:** a rounder bevel of 0.95 h over 12 segments, a restrained satin coat and a deeper press (0.30 × emphasis);
  - **K2:** K1's press and coat with a crisp 0.35 mm edge.
- **Comparison:** composited at 1× and 2× with live vector labels (`buttons_K0_K1_K2_*.png`).

**Touch strip:**
- **Rendered:** the ivory bezel and the stationary charcoal sensor, as separate transparent layers.
- **Live vector:** the orange PITCH and blue MOD indication, the labels and the modulation dot.
- No wheel geometry and no rotating sprites.
- **Still to do:** the rendered sensor reads mid-grey rather than charcoal under the ivory-calibrated exposure, because its satin coat reflects the studio light. It needs its own calibration target, as the graphite mini did, before the layer is usable.

**Later-stage plan:** `docs/reports/gui-assets-later-stage-plan.md` (card surfaces, enclosure, display bezels, keycaps).

## 7. Performance (cumulative: macro D2 + layer-ivory B + the mini-ivory pilot)

**Setup:** a local build of the default branch with D2 and `layer-ivory`, plus the mini pilot wired at the four sites, compared against the vector skin.
- Three layers per instance; paints are whole-editor 2× snapshots at 1448.
- **Timing caveat:** the machine was shared with renders and builds, so absolute milliseconds are inflated. Both skins ran under the same load, so the comparison holds.
- Raw data: `gui-assets-phase4/cumulative-perf.csv`.

| | Vector skin | Rendered skins |
|---|---|---|
| Asset packs in the binary | — | macro 11.7 MB, layer 3.9 MB, mini 4.0 MB (OSP.so 47.0 → 50.9 MB with D2 + layer) |
| Decoded frame memory, 1 editor | 0 | macro 1.4 MB, layer 0.3 MB, mini 0.2 MB (only the frames shown) |
| Decoded frame memory, 4 / 16 / 32 editors | 0 | **3.1 / 0.6 / 0.3 MB, the same at 4, 16 and 32**: one shared asset per process |
| Instances with editors closed (1–32) | 0 | 0: nothing decoded |
| Cold editor open (first instance) | 60 ms | 18 ms |
| First paint | 596–644 ms | 541–624 ms |
| Warm paint (all cached) | 150–153 ms | **131–148 ms** |
| Paint after a resize to 1629 | 896–1015 ms | 835–954 ms |
| Reopen an editor | 228–700 ms | 216–541 ms |

**Reading:**
- **No cost** in paint time, opening or resizing; warm paints are slightly cheaper, because a cached frame is one image draw.
- **Memory:** the rendered families add a few MB of decoded frames per process, shared by every instance.
- **Where the cost actually is:** the resize cost and the large first paint are the ceramic skin composing new sizes (36.7 MB per size at 2×); see the separate ceramic memory investigation.
- **Process memory per scenario** is confounded by allocator reuse between scenarios in one process, so it is not quoted as a cost here.

**Real-time audio:**
- Frames are decoded and resampled lazily in paint, on the message thread. `processBlock` never touches the sprite library.
- The existing `[sprite-knob]` test "the knob skin changes looks only" checks the audio output is unchanged with the rendered skin.

**Still to measure on the Mac** (with the D2 QA build):
- automation and modulation repaint behaviour in Logic and Live;
- CPU when idle with 16 open editors;
- Retina 2× at 1810.
