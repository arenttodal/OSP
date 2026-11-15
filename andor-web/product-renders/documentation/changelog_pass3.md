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
