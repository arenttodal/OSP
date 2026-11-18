# GUI assets · Phase 5 (Package B)

**Scope:** D2 is kept. Layer-ivory B is approved (PRs #26 and #28 stay isolated).

**Phase 5 adds:**
- the mini-ivory pointer refinement;
- the mini-graphite G1 full pack and its pilot in the real wells;
- the K2 key pilot in the real editor;
- the touch-strip sensor;
- three controlled ceramic studies.

**Everything here is local pilot code** in the trial tree (D2 + layer-ivory line): the `sprite` names
on the pilot sites, the packs in `shared/andor-ui/Resources`, and one forced-state hook for the key
captures. Nothing is committed to a product branch. The packs are in the Package B zip.

**Manual tests stay open:** nothing here has been tested on a Retina Mac or in a DAW. The combined
macOS test build is described in `gui-combined-qa-build.md`.

## 1. Mini-ivory P5: pointer 1.0 → 1.3 mm, darker ink

| | |
|---|---|
| Change | `indicator.width` 1.0 → 1.3 mm, `indicator.color` → `#141210`. Cap geometry, material and light unchanged |
| Pack | 129 frames at 48 / 96 / 180 px. Verify: largest indicator error 0.54° |
| Sizes | 48 px 379 KB, 96 px 984 KB, 180 px 2.70 MB (P4: 376 KB / 977 KB / 2.69 MB) |

**Pilot sites:** ADSR, the LFO 1 modulation panel, the FX rack, and LIFE's popover with its secondary row.
- **Widths:** 869 px at 1×, 1448 and 1810 px at 2×.
- **Compared:** vector / P4 / P5 (`b/miniivory/`).

**Result:**
- **At 869 px, 1×** (the smallest popover knobs, about 16 px): the P5 pointer reads as a clear dark line, as legible as the vector pointer. P4's lighter, thinner line greyed out at that size.
- **At 2×:** the pointer is firmer and still in proportion with the cap.
- **Unchanged:** arcs, captions and values stay live and identical.

**Recommendation:** adopt P5 for the mini rollout.

## 2. Mini-graphite G1: full pack and the well pilot

| | |
|---|---|
| Pack | G1 (lighter rim, dark centre, light pointer `#e8e2d8`), 129 frames at 48 / 96 / 180 px: 405 KB / 1.07 MB / 2.91 MB |
| Indicator check | 0.57° worst over 129 frames, measured on the light pointer |

**About the verify failure:** the stock `verify.py` reports a failure because it locates the indicator from the *dark* pixels, which on graphite are the cap. With the light pointer measured instead, the frames check out. A `--light-indicator` option for `verify.py` is a small follow-up.

**The pilot:**
- **Where:** `sprite = mini-graphite` on the granular well's knobs (shared `EngineCard`: OSP, and ANALOG where `graphiteOnWell`) and on ANALOG's synthesis-well knobs (`AnalogWellOverlays`, both sites).
- **One local drawing change:** a rendered cap may also draw on a graphite knob. The groove, arc, labels and values stay live.
- **SPACE's boxed DECAY:** untouched.

**Where the pilot was built:**
- **OSP:** the trial tree.
- **ANALOG:** its isolated D2 build (`claude/analog-pin-d2` with the same three local edits, reverted after the capture).
- **Why not the trial tree for ANALOG:** its tests do not compile against the default line, as the dependency audit found.

**Captures:**
- **OSP:** the granular well as it shows on hover, 1448 @2x and 869 @1x.
- **ANALOG:** `[screenshots-three]` (51 states at 869 / 1448 / 1810), with `OSP_KNOB_SKIN=original` as the before.
- **Sheets:** `b/wells/`.

**Result:**
- **OSP granular well:** the dark caps sit *into* the display instead of standing out against the
  waveform as the ivory vector caps do. The light pointer reads clearly at 869 px, 1×. The layer-coloured arcs and the
  values are live and unchanged.
- **ANALOG synthesis wells (OSC 1–3) and granular wells:**
  - G1's lighter rim and light pointer read at every width, including 869 px;
  - dimmed knobs (WIDTH, LFO PW when inactive) still dim as before;
  - the arcs (accent colour) and values are live.
- **A change of identity to accept consciously:** ANALOG's vector graphite cap has a *bronze* rim; G1's rim is a neutral light grey. Next to ANALOG's bronze accents this reads cooler. Taking G1 into ANALOG means accepting that, or rendering a G1-bronze variant: the same pack with the rim colour only.
- **The macro row** differs between the two ANALOG captures only because `original` also turns D2 off. That is expected.

**Recommendation:** G1 is ready for OSP's granular well. For ANALOG, decide bronze versus neutral rim first.

## 3. K2 keys in the real editor

**The pack:** the shipped `ceramic-skin.zip`, with only its 44 key pieces (88 images, 4× and 2×) re-rendered in K2 form:
- a crisp 0.35 mm edge;
- a restrained satin coat;
- a deeper press (0.30 × emphasis) for pressed and active.

The manifest format and every other piece are unchanged. Dimensions, icons, typography and hitboxes are untouched: the pieces render at each key's real rectangle.

**The states captured** (1448 @2x and 869 @1x):
- normal, hover, pressed and active, each forced on every surface-drawn key;
- the real states, as-is and with ARP on and B muted;
- the editor disabled.

**The regions:** the three card headers (source-card buttons, mute/solo, icon keys) and the bottom-right housing (ARP / SUB and the keyboard utility keys).

**Measured** (`b/k2/`: mean luminance change from *normal*, and the pixels changed by more than 6 levels):

| | Hover | Pressed | Active | Disabled |
|---|---|---|---|---|
| Housing 1448@2x, A1 shipped | 0.84 / 0 | 0.29 / 1,531 | 2.13 / 23,248 | **0 / 0** |
| Housing 1448@2x, **K2** | 0.83 / 0 | **0.36** / 1,472 | 2.23 / 24,158 | **0 / 0** |
| Housing 869@1x, A1 / **K2** | 0.83 / 0.79 | 0.24 / **0.33** | 2.08 / 2.17 | 0 / 0 |
| Headers 1448@2x, A1 / **K2** | 0.62 / 0.62 | 0.31 / **0.38** | 1.38 / 1.55 | 0 / 0 |
| Headers 869@1x, A1 / **K2** | 0.60 / 0.61 | 0.27 / **0.34** | 1.33 / 1.43 | 0 / 0 |

- **K2 at rest:** the keys differ from A1 by a mean of 0.94 (housing) and 0.53 (headers). The crisper edge reads as precision, not as a new design.
- **Pressed:** K2 moves 20–40 % further from normal than A1, so the deeper press is real. On its own it is still a subtle cue.
- **Selection in the product does not rest on the press:**
  - *active* carries the accent tint;
  - the real selections carry colour: ARP's lit lamp, MUTE / SOLO's own colour, EQ's telltale.
  - With K2 this rule holds. **Do not** let the deeper press become the only selection cue anywhere.
- **Hover:** a uniform slight lift (no pixel changes by more than 6 levels) in both. Fine for a pointer-feedback state.

**Recommendation:** K2 is a safe refinement and can replace A1 for the key pieces. It needs the
disabled design below before it ships.

**Findings that apply to A1 and K2 alike:**
- **Disabled is not drawn.** Surface keys render at full opacity, and in these regions the ink does not dim either, so a disabled editor looks the same as a normal one. This needs a design decision: for example, the key at about 45 % opacity, as the knobs use.
- **The utility keys draw themselves.** The LIBRARY, SETTINGS and INFO keys are not surface-drawn keys, so they keep their own vector states and are not affected by a key-pack change. Bringing them under the skin is a separate, small change.

## 4. Touch strips: dark charcoal sensor

**Recalibration:**
- The sensor material is calibrated to the editor's own MOD sensor: `#131617`, roughness 0.42, a light coat.
- Measured luminance 51.8 against the editor's 52.3.
- It is rendered as its own layer inside the ivory bezel layer.

**The study:** `b/strip/strip_study.png`, from the performance capture at 6.6 s with MOD lit:
- the rendered bezel and sensor are composited at the editor's strip openings;
- the PITCH and MOD indication is the editor's own pixels, laid on top.

**Result:**
- **Holds up:** the layers stay separate and the live indication reads unchanged.
- **Flatter:** the rendered sensor is flatter than the vector one, which has a gentle top-to-bottom gradient and two guide lines.

**Recommendation:** keep the guide lines as live vector on top of the rendered sensor, so the strip keeps its sense of travel.

## 5. Controlled ceramic studies

**Method:**
- Rendered through the skin's own pipeline (`ceramic_study.py`, the same studio, materials and exposure as `render.py`).
- Patched into the K2 pack, only the three pieces (`patch_pack.py`).
- Built and captured in the real editor: three layers, 1448 @2x and 869 @1x (`b/studies/`).

**Not changed:** layout, size, colour and the approved material identity.

| Variant | Card surface (`card-451`) | Display bezel (`bezel-r9`) | Enclosure (`housing`) | Mean change from V0 (whole editor, 2×) |
|---|---|---|---|---|
| V0 | Shipped definition | Shipped | Shipped | – |
| V1 | Rounder 0.8 mm shoulder | Rounder 0.5 mm lip | Finer, slightly stronger powder grain | 0.24 levels, only on the card edges and bezel lips |
| V2 | A little more lift (1.2 mm) | Deeper 0.9 mm recess | A touch more satin | 0.35 levels: card edges, bezel recess, and the whole enclosure evenly |

**Seen at 3×:**
- **V1's shoulder** turns the card's top edge into a softer roll of light, which reads more like a glazed ceramic edge. Its lip catches a slightly broader highlight.
- **V2** gives the card a firmer edge and the display recess a darker, deeper rim, so the displays sit more "inside" the card.
- **The enclosure changes** (grain, satin) are not visible at normal viewing size in either variant.

**Recommendation for review:**
- Combine **V1's card shoulder with V2's deeper bezel recess**, and leave the enclosure as shipped.
- All three changes are near the threshold of perception, so they are refinements, not a redesign. They are only worth making as one pass together with K2 (one pack update, one visual-regression baseline).
- **The keyboard is the next gate,** as the brief says. It was not touched.
