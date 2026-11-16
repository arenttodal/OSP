# Pass 3 change log (Round 1: physical quality)

Pass 2 is preserved:
- `blender/andor_master_pass2.blend` (git-ignored, regenerable);
- `blender/scripts/andor_scene_pass2.py`, a frozen copy of the script.

Pass 3 is `andor_scene.py`, with the scene saved as `andor_master_pass3.blend`. Materials, lights, exposure,
and cameras A/B are unchanged in Round 1, so every comparison isolates geometry.

## Ground truth used

| Reference | Authority for | Source |
|---|---|---|
| The approved macro knob | knob shape: the editor's knob sprites are rendered from this generator, and every knob in the editor uses that one cap, scaled | `tools/blender-assets/config/macro-knob.json`, `osp_assets/geometry.py`; report `docs/reports/knob-phase1.md` (direction A, "shallow, tapered, fluted") |
| Approved skin, direction 09 "Warm Ceramic + Teak" | heights, bevels and radii of the housing, cards, macro panel, keys, badges, bezels, keybed, lens LEDs, fader and guide | `design-lab/blender/configs/controls.json`, `tools/blender-assets/skin/pieces.py` (drawn at 0.31 mm per editor px; converted to this scene's 0.276 mm/px) |
| Editor capture | positions, pointer angles, artwork | `web-10-hero-3L-nocaps` (+ JSON) |
| Granello | light, shadow and camera behaviour only | owner's reference (not stored) |

The editor's earlier close-up shows the old vector knob (a flat cap in a ring). The plugin has drawn the
rendered fluted cap since knob phase 1, so the fluted cap is the reference here.

## Objects changed (before → after)

| Object | Pass 2 | Pass 3 |
|---|---|---|
| **Macro knobs (6)** | own profile: 8.0 mm high on a 25.7 mm cap (0.31 D), rounded rim roll, 0.55 mm dome, knurl ±0.011 r, dark collar, pointer as a raised strip 0.035 mm proud | **the approved generator's profile**, variant E: base ring + 0.3 mm groove, 7 % taper, **0.25 D** high, 0.9 mm bevel (0.032 D), 0.4 mm crown, 48 flutes 0.26 mm deep (0.009 D, fading in above the groove and out across the bevel). The pointer is engraved in the cap's own material (flush; width 1.3 mm, from 0.30 of the top radius to the bevel), at the editor's angle. Body radius = the editor's 46.48 px |
| Layer (15) and envelope (4) knobs | own profiles, 5.5 / 4.5 mm | the same approved profile, scaled to the editor's radii (26.06 / 20.0 px), same proportions (480 angular samples) |
| Knob material | ivory 245 241 233, roughness 0.30, coat 0.25 | **Satin Ivory, as approved** (`materials/ivory.json`): #ece8e0, roughness 0.36, specular 0.5, coat 0.18 / 0.22; ink #26231f at roughness 0.72 |
| Teak frame | butt joints; front and back full width | **mitred** at all four corners (each rail runs to the outer-inner diagonal), 0.7 mm bevel on every edge; grain along each rail; no end grain |
| Timber height | 16 mm | test: 16 / 14.4 / 12.8 mm (cameras fixed) |
| Panel seat | 1.0 mm below the teak | 1.34 mm (spec 1.5 mm) |
| Layer cards | 1.2 mm, 0.4 mm bevel, radius 9 px | **0.80 mm** (spec 0.9), 0.28 mm bevel (spec 0.55; crisper, brief §3.2), **radius 16 px** |
| Macro panel | 1.2 mm, radius 9 px | **0.53 mm** (spec 0.6), radius 13 px |
| Display wells | 0.85 mm deep, display bounds, radius 8 px | **0.53 mm** (spec 0.6) with a 0.28 mm lip, outline grown 1.6 px (bezel), radius 9 px (sources) / 12 px (envelope) |
| Keys (31) | 0.8–1.3 mm, 0.35 mm bevel, radii 4–9 | **0.8 mm × role emphasis** (0.43–0.71 mm), bevel ⅓ of the height (planar tops), radii per role (4–12 px) |
| Key bed | raised 1.0 mm | **a pocket** 1.07 mm deep (spec 1.2) in the housing; black keys stand 2.2 mm above its floor (as pass 2) |
| New: layer badges A/B/C | (flat artwork) | 47 × 43 px, radius 7, 0.62 mm, coloured sides |
| New: lens LEDs | (flat dots) | six lenses under the macros, 9 px, 0.67 mm; CHARACTER lit (as `litMacros`) |
| New: volume fader | (flat artwork) | C1 ivory cap 14 × 22 px, 1.8 mm, on the T3 warm brushed-aluminium guide (the only metal in the design) |

## Not changed in Round 1

- **Materials:** ceramic, card, display, bezel, teak, plinth. The knob material is the exception, set to the
  approved spec.
- **Lights, exposure and view transform.**
- **Cameras A and B.** Their targets now use fixed reference heights, so the timber test cannot move them.

## Known limitations

- **Knob height** is an open decision for the owner. The repo spec is 0.39 D; the brief asks 0.11–0.17 D; the
  candidate is 0.25 D. All variants are in Test 01.
- **Buttons** keep the editor's 1–2 px drawn contact line under the raised key.
- **Pitch and mod wheels** are still flat artwork (Round 2).
- **Timber texture and colour** are unchanged (Round 2).

# Pass 3.5, checkpoint 1: knob height (locked: G, 0.30 D) and timber (locked: 14.4 mm)

Test: five heights with the same camera, lights, materials and exposure, each in four views (the round 1 close-up, a
lower 22° inspection angle, the six macros, and the full product from camera B). Each knob is measured from the built
mesh (`andor_scene.py --report`); see `renders/review_pass3_5_cp1/measurements.md` and `report_*.json`.

| | H/D | Height (design, D = 28 mm) | Profile |
|---|---|---|---|
| E | 0.25 | 7.00 mm | round 1 (0.9 bevel, 0.4 crown, 1.1 base ring) |
| F | 0.27 | 7.56 mm | official, only the height changed; flutes 0.252 mm |
| **G** | **0.30** | **8.40 mm** | **official, only the height changed; flutes 0.252 mm (locked)** |
| H | 0.33 | 9.24 mm | official, only the height changed; flutes 0.252 mm |
| D | 0.393 | 11.00 mm | official, unchanged (flutes 0.42 mm) |

- **Height convention:** the generator's own `height` runs from the mounting plane (z = 0) to the crown's top, base ring
  included, so nothing is added twice. The profile is regenerated per height (never stretched), and then uniformly
  scaled to the editor's knob radius (×0.889: D = 24.9 mm and G = 7.46 mm in the scene; proportions unchanged).
- **Why G:** E reads low. F is a subtle step. G gives a visible, substantial sidewall with the crisp bevel and fine
  flutes intact. H starts to read bulky, and D reads tall at the product-photography angle.
- **Marketing vs plugin knob:** the marketing knob is 0.30 D with 0.252 mm flutes; the plugin's knob is
  0.393 D with 0.42 mm flutes. The shipping sprites are unchanged.
- **Timber:** default `--frame-h 14.4` (rails z 2.0–16.4 mm, all four equal; mitres regenerated, not scaled).
- **Automated checks (all variants):** six macros; identical diameter and height; all 25 knobs seated with a 0 mm gap;
  48 flutes counted on the mesh; the rails the same height; positive clearances (14 px to the lens LED, 53 px to the
  neighbour); pointer angles identical across variants.
- **Saved:** `andor_master_pass3_5.blend` (compressed, textures packed; regenerate with `--knob hd --hd 0.30
  --frame-h 14.4 --save-blend`).

# Checkpoint 2: final geometry synchronisation with the approved GUI (trimmed footer, ivory front return)

**Reference.** The approved, locked OSP interface is commit `ef5c0c3` on `claude/friendly-pascal-0y48r0`. It is
1448 × 1046 editor px: the keyboard is unchanged; PITCH/MOD are back in their original places; a 2 px reveal and a
20 px soft apron sit under the keys; there is no footer. A fresh capture of that build (`[web-capture]`,
`web-10-hero-3L-nocaps@4x` 5792 × 4184 + knob JSON, `web-08` bounds) drives the model. All 25 knobs, the cards,
displays, buttons and keys come from it. Their editor geometry is identical to pass 3.5 above the keyboard (checked:
the same 25 knob centres, radii and angles; the same keyboard rect 148, 913, 1150 × 101; the same 36 black keys).

**Master.** `andor_master_cp2.blend` is a new file; `andor_master_pass3_5.blend` is kept unchanged. Both are built by
`andor_scene.py` (regenerate with `--knob hd --hd 0.30 --frame-h 14.4 --save-blend`). Materials, lights, exposure and
every existing camera are unchanged, so all differences in the review are geometry.

## What changed (before → after)

| Part | Pass 3.5 | Checkpoint 2 |
|---|---|---|
| Interface texture | 1448 × 1086 capture with the "Ready" footer below the keys | the approved 1448 × 1046 capture: keys, reveal, soft apron; no footer |
| Texture diff (4x, old vs new) | | identical everywhere above y 1014 except x 41–131, y 916–1014: the PITCH/MOD captions, where the approved GUI shows the full MOD C caption and socket circle (pass 3.5's capture clipped them) |
| Front of the housing | a full-width teak fascia (14.4 mm high), mitred to the cheeks, with 55 px (15 mm) of ceramic panel between the key fronts and the wood | **no fascia**. The ceramic panel ends 8 px (2.2 mm) after the keybed, then the **ivory front return** |
| Ivory front return | (none) | one piece with the panel (so the soft apron has no part line, as in the GUI). Top: the apron, 22 px (6.1 mm) deep in plan, printed from the capture, flush with the panel; a 1.2 mm radius edge; a 6.0 mm vertical face (= the GUI apron's 22 px); a 0.45 mm round below. It spans cheek to cheek. Face colour: the GUI apron's front tone (212 204 190), so the ceramic falls off from the printed apron as the editor's soft shade does |
| Underside | the plinth, inset 3 mm | a dark base set back 3.0 mm behind the return's face, flush with the plinth's front: the return reads as a shallow lip with a shadow line under it, not a front wall |
| Side cheeks | mitred into the fascia at the front | run to the new front edge (1046 px) and end square: end grain at the front, flush with the return's face. Still 14.4 mm (z 2.0–16.4 mm), mitred to the back rail |
| Ceramic tray | to the fascia | ends under the return |
| World origin | the face centre of the 1086 px editor | unchanged (fixed at 543 px), so the old and new instruments share every camera and every pixel above the keyboard |

Unchanged: the G macro knob (0.30 D, 8.40 mm design height, 7 % taper, 48 flutes 0.252 mm, pointer angles from the
capture), all knob positions, cards, wells, buttons, badges, LEDs, fader, keybed pocket and black keys. The geometry
report (`renders/review_checkpoint2/report_cp2.json`) repeats the pass 3.5 checks.

## Like-for-like comparison

The pass 3.5 instrument is rebuilt by the same script from the old capture (unpacked from
`andor_master_pass3_5.blend`; `geometry.json` without an `apron` entry rebuilds the fascia). The old and new renders
therefore share cameras, lights, materials, samples and resolution. Camera B "before" is pass 3.5's `G_neutral.png`
(the same settings).

## New cameras

- `front_edge`: the product square to the camera, 8° above the floor, 135 mm lens (the straight-on bottom edge).
- `keys34`: a three-quarter close-up of the key fronts and the return.
- `corner_fl` / `corner`: the existing lower-left and lower-right corner cameras.

## Known limitations (Round 2)

- The white keys are still the capture on the pocket floor; only the 36 black keys are raised.
- The PITCH/MOD wheels are still flat artwork.
- The pale band along the cheek tops is the rim light on the teak's oiled sheen. It is the same in pass 3.5.

# Keyboard + front body round (after checkpoint 2)

Brief: the instrument read too thin, the white keys read as a printed strip, and the front return under the keys
(a lip over a dark band) was unresolved. This round changes construction and proportion only. The plan footprint,
the approved GUI, the keyboard span, the G knobs (0.30 D), the layout and the 14.4 mm teak cheeks are unchanged.
Lights, exposure and cameras are also unchanged. The checkpoint 2 scene is frozen as `andor_scene_cp2.py`, and its
renders are the "previous" side of the comparison.

**Owner's decision (thickness):** the body gains mass in ivory, not wood. The cheeks stay 14.4 mm and stand on an
ivory lower shell. The total height goes from 16.4 mm to 20.7 mm, about 5 % of the width.

## Section (scene mm, from the floor of the feet; `report_r2.json` → `section_mm`)

| Level | z (mm) | |
|---|---|---|
| teak cheek top | 20.70 | cheeks z 6.30–20.70 (14.4 mm, unchanged) |
| black key tops | 20.76 | 2.0 mm above the white keys |
| ivory top shell | 19.36 | 1.34 mm below the teak, as before |
| white key tops | 18.76 | 0.6 mm below the shell, inside the keybed opening |
| front return (ledge) top | 15.76 | 3.6 mm below the shell: 3.0 mm of every white key front shows above it |
| white key bottoms | 15.36 | 3.4 mm bodies |
| keybed floor (dark felt) | 14.76 | seen only through the key gaps and the slot under the key fronts |
| lip bottom | 10.76 | the front return's 5.0 mm lip, 1.5 mm nose |
| part line (lower shell top) | 6.30 | |
| lower shell | 0.80–6.30 | 5.5 mm, 3.0 mm radius tuck-under, 0.4 mm inside the cheeks; at the front stepped back 2.0 mm with the body under the lip |
| feet | 0–1.2 | four dark discs, 10 mm, 19 mm in from each corner |

## Construction (before → after)

| Part | Checkpoint 2 | This round |
|---|---|---|
| White keys | the capture on the pocket floor | **52 solids** (22.115 px pitch, measured from the capture): 3.4 mm bodies, a 0.5 mm nose on the top-front edge, 0.15 mm edge rounds, 0.24 mm gaps, cut-outs 0.3 mm clear of the black keys. Tops carry the capture (note names, zone colours) in a key-plastic finish (roughness 0.30, a little coat), sides and fronts in key-white plastic (246 242 234) |
| Black keys | slabs 2.2 mm over the pocket floor, capture on top | **36 tapered solids** from the keybed floor (0.22 mm side draft, a 1.1 mm sloped front), 2.0 mm above the whites, 0.32 mm edge rounds, satin charcoal (roughness 0.52, specular 0.30), no printed top |
| Keybed | a 1.07 mm pocket | an opening in the top shell (146–1300 px), open to the front; a dark felt floor; a real 0.8 mm shadow slot between the key fronts (1014 px) and the front return (1017 px) |
| Top shell beside the keys | the panel to 1024 px, a 6 mm return in front of it | the shell runs at full height to the front edge as end blocks under PITCH/MOD and under ARP, with the same 1.5 mm nose. Only the keyboard section drops to the lip, like a keybed cut-out. Each front corner is one ivory block against the cheek |
| Front return | one piece with the panel, a 6 mm face over a dark base | in the keyboard section, a ledge 3.6 mm below the shell, printed with the GUI's apron, 1.5 mm nose, 5.0 mm lip |
| Under the lip | a dark base (plinth material) | the ivory body, stepped back 2.0 mm: the lip's shadow is cast by real construction. **No dark band** |
| Base | dark plinth | the ivory lower shell (5.5 mm) with a 3 mm tuck-under on four small feet |
| Side cheeks | 14.4 mm on the plinth | 14.4 mm on the lower shell (0.4 mm shadow line at the joint); front ends flush with the end blocks |
| Teak tone | saturation 1.12, value 0.74 | hue +0.012, saturation 0.80, value 0.57: deeper golden brown, less orange and pink |
| End grain | a scaled copy of the long grain (read as stacked layers) | quarter-sawn rings: fine near-vertical lines, darker and more matte, low contrast |

## Review set

A front low angle (`front_edge`) · B keyboard close-up (`keys34`) · C left-front corner (`corner_fl`) · D
right-front corner (`corner`) · E neutral three-quarter (camera B) · F previous vs revised, same cameras. Saved as
`andor_master_r2_keyboard_body.blend`.

## Still open (next rounds)

- PITCH/MOD wheels are still the capture (flat artwork) on the end block.
- Lighting, broader materials and hero/campaign renders await approval of this geometry.
