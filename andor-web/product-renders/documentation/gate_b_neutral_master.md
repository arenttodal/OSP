# Gate A + Gate B: GUI verification and the neutral 3D master (16 Nov 2026)

## Gate A: GUI bottom-edge, verified in the real editors

**ANDOR OSP: verified.** The trimmed footer is in the shipping editor, not only in capture code:

- `PluginEditor::instrumentHeight()` uses `trimmedHeight`, so the default size is 1448 × 1046 px at 1.0 (`setSize` in the
  constructor).
- `frame::fit` and `frame::applyWindowShape` use the same height.
- Resize limits run from 869 to 1810 px wide (`InstrumentFrame.cpp`), at the instrument's aspect ratio.

| Check | Evidence | Result |
|---|---|---|
| Whole suite (ctest, plugin build) | plugin, osp_visual_reference, memo_lab, andor_example_tests, andor_example_tests_visual (shell parity), bootstrap; then unit, integration, regression, performance-smoke | **10 / 10 passed** |
| Preferred size, no footer reservation, every scale | "OSP's trimmed footer - the instrument ends under the keys at every size" (widths 869 / 1086 / 1448 / 1810 and ARP open): wheel bottoms at the apron, captions and sockets inside their bounds, ≥ 7.5 px to the apron, ≥ 6 px horizontal clearance, hitboxes, rendered clearance, rail shadow continuous through the apron | **passed** (118 assertions) |
| Keyboard input, note highlighting, pitch/mod input | "ARP plays held notes …; the screen keyboard, wheels and pedal" | **passed** (21 assertions) |
| Sister product follows OSP | andor_example_tests_visual (shell vs OSP, pixel for pixel) | **passed** |
| Hidden UI test: ARP keyboard control and inline editor | 132 / 135 | **3 failures, pre-existing, unrelated**: the overlap check finds the ARP panel's RATE knob and two children at the identical rectangle (880, 58, 100 × 94). That is an intentional overlay. The panel changed on 4 Nov (per-card arpeggiator); the check dates from 12 Oct; no footer commit touches `ArpSection` |
| Hidden UI test: assignment, wheel, START, Granular drops | 81 / 89 | **8 failures, pre-existing, unrelated**: MODULATION tab spacing (11.6 px vs 14) and menu wording ("Assign ENV 2", "Edit ENV 1"). The panel gained MIX and SYNTH destinations on 30 Oct and 1 Nov; the test dates from 12 Oct; the footer commits only added `showNoticeAt` |

The two hidden tests are not part of the default run. They are recorded here, not fixed, because they are outside
the footer task.

**ANDOR ANALOG: not yet integrated.** ANALOG builds the shared shell from `external/osp`, pinned at `de3253c` (the
`claude/andor-analog-shell` line). That commit has ten later shell and rack commits but **none of the seven
trimmed-footer commits**. A trial merge (in a scratch worktree, then discarded) conflicts in eight shared files:

- `ShellEditor.cpp/.h`
- `ShellProcessor.h`
- `InstrumentFrame.cpp/.h`
- `AndorUi.cmake`
- `PluginProcessorTests.cpp`
- `STATUS.md`

Closing Gate A for ANALOG needs:

- an owner decision on which branch carries the merge;
- commits in OSP and a submodule bump in ANALOG;
- then ANALOG's build, tests and screenshots, keeping SUB and the auxiliary controls.

## Gate B: the neutral 3D master

Baseline: `andor_master_r2_keyboard_body.blend` (kept, with `andor_master_pass3_5.blend` and `andor_master_cp2.blend`
as rollback points). New master: `andor_master_gateB_neutral.blend`. Regenerate it with `andor_scene.py` defaults
(`--knob hd --hd 0.30 --frame-h 14.4 --finish refined --teak warm --light sculpt`). The earlier looks stay
reproducible with `--finish r2 --light neutral`.

### Geometry corrections (small, within the existing model)

| | Approved round | Gate B |
|---|---|---|
| Front return | lip with a rounded top edge over a body stepped back 2.0 mm | the lip's **underside is rounded too** (1.5 mm, the same as its nose, on the lip and on the end blocks): the front reads as the shell's own moulded return, not a plate laid under the keys. The setback is **1.2 mm**, a shadow line rather than a shelf |
| Timber edges | 0.7 mm | 1.0 mm on the cheeks and back rail, in step with the ivory's softer radii (proportions unchanged: 14.4 mm) |
| Keys | — | unchanged (checked: 52 whites of equal thickness and nose, 0.24 mm gaps, blacks 2.0 mm above, shadow slot under the fronts) |

### Materials

| Part | r2 | Gate B ("refined") |
|---|---|---|
| Housing (capture) | roughness 0.48, specular 0.22, coat 0.05 | 0.50 / 0.20 / 0.04 |
| Source cards (capture) | = housing | 0.43 / 0.26 / 0.08, 0.6 % warmer |
| Macro panel (capture) | = housing | 0.46 / 0.23 / 0.06 |
| Buttons and badges (capture) | = housing | 0.38 / 0.30 / 0.10, 0.6 % cooler |
| White keycaps (capture) | 0.30 / 0.42 / 0.12 | satin key plastic 0.26 / 0.48 / 0.18 |
| Front return | 0.40 | 0.44 (the same ceramic, a little quieter than the keycaps) |
| Knob caps | approved Satin Ivory | unchanged |
| Black keys | satin charcoal 0.52 / 0.30 / 0.04 | 0.46 / 0.32 / coat 0.10: clean highlights along the 0.32 mm edge rounds, still satin |
| Displays | coat 0.45, coat roughness 0.03 | coat 0.34 / 0.045: less glare. The waveform pixels are untouched (same image, same mapping) |
| Bezel | roughness 0.32 | 0.26 + coat 0.25: a fine highlight on the lip |
| Teak | one tone, straight fibres, uniform sheen | fibres wander slightly (no ruler-straight lines, no visible repeat of the stacked strip), tone drifts ±5 % and sheen ±0.06 along the board. Three finishes compared; **deeper warm satin** chosen (hue 0.507, saturation 0.92, value 0.47, coat 0.16 at 0.28) |

### Lighting ("sculpt")

| Light | Neutral rig | Sculpt rig |
|---|---|---|
| Key | 1.15 × 0.85 m soft box, upper left, power 26 | **0.75 × 0.55 m**, raking from upper left-behind, power 25: knobs, flutes, cards and key fronts model; short soft shadows |
| Overhead | — | 1.8 × 1.2 m, power 3.5: lifts white-on-white without flattening it |
| Fill | power 4.5 | 2.4, cooler: shadow sides keep depth |
| Rim, front kicker, display card, black flag | — | unchanged |

Real lights only: no glows, overlays or AO tricks. The exposure and view transform (AgX Punchy) are unchanged.

## The marketing model vs the real plugin (deliberate differences)

| Element | Plugin (GUI) | 3D marketing model | Why |
|---|---|---|---|
| Macro knob height | 0.393 D, flutes 0.42 mm | **0.30 D**, flutes 0.252 mm (G) | approved for product photography; the shipping sprites are unchanged |
| White / black keys | drawn | solid keys with fronts, gaps, cut-outs; black keys without the drawn lighter front cap | a physical keyboard; the plan positions are the GUI's |
| Keybed front | a flat 8 px strip, then the apron | a 0.8 mm shadow slot under the key fronts, then the lowered lip | the keys' fronts must show |
| Bottom edge | the 20 px soft apron | end blocks at full height beside the keys; the keyboard section dropped to a 5 mm lip, printed with the apron | the same plan, built in section |
| Body below the panel | (none) | ivory lower shell 5.5 mm, four feet; total 20.7 mm | owner's thickness decision |
| PITCH / MOD wheels | drawn | still drawn (flat artwork) | Round 2 item, still open |
| Teak tone | the editor's teak | deeper warm satin, natural grain | Gate B choice |
| Ceramic parts | one palette | ±0.6 % temperature and roughness steps between housing, cards, macro panel and buttons | separation under real light |

## Review set (`renders/review_gate_b/`)

`00_contact_sheet`, `01_camera_A`, `02_camera_B`, `03_keyboard_macro`, `04_left_front_corner`, `05_right_front_corner`,
`06_macro_knob`, `07_material_comparison` (wood: r2 / current tone + natural grain / neutral / warm; ceramic: r2 /
refined; the same camera and rig), `08_low_front`, `09_before_after` (approved round vs Gate B, same cameras) and
`report_gateB.json`. Full-resolution JPEGs: cameras A and B at 2560 × 1920, details at 2000 × 1500, the low front at
3200 × 1200.

## Known issues

- PITCH/MOD wheels are flat artwork.
- The product floats 55 mm above the floor in the neutral stage. This is a presentation choice; the feet are
  modelled.
- ANALOG integration (above).
