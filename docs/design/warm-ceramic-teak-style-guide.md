# Style guide: 09 Warm Ceramic + Teak

This guide lists every change from today's GUI to the chosen direction. It is also the
spec for the production build. The renders are in `design-lab/` (direction
`09-warm-ceramic-teak`). Colours marked *displayed* were sampled from the 1× render, so
they are what the eye sees after the light falls on the material.

**What does not change:**
- the layout (every position and size);
- the text, fonts, values, waveforms, value rings and modulation halos;
- the dark displays;
- the orange accent `#e8692a`;
- the macro colours;
- the approved Blender macro knob;
- the vector layer and mini knobs;
- the painted divider.

## 1. Light

| | |
|---|---|
| Source | The approved knob's studio: a soft overhead key light, top-left. Neutral white light, no warm cast. |
| Shadows | Soft contact shadows, falling down and slightly right. They are rendered, not drop-shadow blurs. |
| Rule | Every rendered part uses this one light, so the knob, cards and keys read as one object. |

## 2. Enclosure

| Element | Today | 09 |
|---|---|---|
| Backdrop | gradient `#d6ccc2` → `#e3dacd` | warm taupe, *displayed* `#b7a89c` |
| Housing | 26 px corners, painted gradient `#eae3d6` → `#ded5c8` | **7 px corners**, so it meets the wood squarely. A real 6 mm slab with a 2 mm rounded edge and a contact shadow. *Displayed* `#e9e2d4` → `#e0d9cb`. |
| Teak cheeks | none | New, left and right (§3) |

## 3. Teak cheeks

| | |
|---|---|
| Position | In the window's existing 25 px side margins, so nothing moves. Left (3, 13, 22×1064); right (1423, 13, 22×1064). |
| Form | 3 px corners. They stand 1.5 mm proud of the housing. The edges are rounded and oiled (1.1 mm bevel). |
| Material | Oiled teak: base `#b97a45`, grain `#c4834a` / `#7e4a24`. The grain runs straight along the length. Satin: roughness 0.5, light clear coat. |
| *Displayed* | `#c28c68` / `#b98664` |
| Build | A 3-slice: end caps plus a tiled middle, so the cheeks follow the window height when the ARP panel opens. |

## 4. Cards, panels and recesses

| Element | Today | 09 |
|---|---|---|
| Layer cards | Painted gradient `#f5eee4` → `#e9e2d6`, 16 px corners | **Satin ceramic ivory**: a 0.9 mm slab with a 0.55 mm soft edge and a faint sheen. *Displayed* top `#fcf5eb`, middle `#f9f2e8`, bottom `#f7f0e5`. Corners unchanged. |
| Display bezels | A flat dark well | Each dark display sits in a **1.6 px ceramic lip**, 0.6 mm deep. The well colours are unchanged. |
| Macro panel (D3) | Painted gradient `#f2ebe0` → `#e6dfd2` | **Satin ceramic**: raised 0.6 mm with a 0.5 mm soft edge. *Displayed* `#f3ede1` → `#f2ebdf`. |
| Keybed | Flat | **Recessed 1.2 mm** with a 0.4 mm lip and 6 px corners. *Displayed* floor `#efeadf`. |
| Divider | Painted | **Unchanged** (painted divider) |
| Title | "MACROS" | **Removed** (done, commit e5d62e2) |

## 5. Buttons: A1 Soft Ivory

All buttons become shallow physical keys: an 0.8 mm soft-polymer key with a 0.55 mm
rounded edge and a soft contact shadow. The icons, labels and their colours are unchanged.

| State | Look |
|---|---|
| Normal | Ivory face, *displayed* `#f2ece4` |
| Hover | The face lifts 16 % toward white |
| Pressed | The key sinks to 0.42 mm, so its shadow shortens |
| On (active) | It sinks like pressed and takes a warm peach face (`#f2c8b0` at 50 %), *displayed* `#f8e0cf`. The icon keeps the accent colour. |
| Disabled | The normal key with the existing software wash |
| Focus | The existing software focus ring |

**Emphasis by role.** The key height is scaled by role, so the hierarchy reads in depth:

| Role | Scale |
|---|---|
| modifiers | 1.0 |
| ARP | 0.9 |
| mode, EQ, Advanced | 0.8 |
| preset | 0.7 |
| layer state | 0.6 |

**Large keys.** The preset bar (*displayed* `#faf4eb`) and the mode selector (`#f8f2ea`)
use the panel colour, so they don't read as big keys.

**Corner radii in use:** 4, 6, 7, 10 and 12 px.

## 6. Layer badges (A/B/C)

The badges become rendered ceramic tiles: 47×42 px, 7 px corners, 0.7 mm high with a
0.4 mm edge. The letters are unchanged.

| Badge | Colour | *Displayed* |
|---|---|---|
| A | `#c4561f` | `#d87c60` |
| B | `#4d6176` | `#808994` |
| C | `#64806b` | `#b7c1b4` |

## 7. Tabs: B1 Refined Flat

This change is vector only.

| | Today | 09 |
|---|---|---|
| Underline | A 1.6 px bar | A **2 px accent bar with round ends**, 19.6 px below the strip top |
| Selected word | Text colour at 92 % | **Full text colour `#23211e`** |
| Idle words | Text colour at 50 % | **Quieter**: 88 % of today's, so 44 % |

## 8. Volume fader: C1 ivory on T3

| Part | Spec |
|---|---|
| Cap (C1) | Warm ivory, the approved knob's satin. 14×22 px, 4 px corners, 2 mm high with a 0.6 mm soft edge. **Three fine grip grooves** run across the direction of travel. *Displayed* `#c8c4bd`. |
| Track (T3) | A **fine metal guide rail**, 3.2 px wide, standing 0.6 mm with a 0.25 mm edge. Warm brushed aluminium, base `#958c7e`, roughness 0.4. *Displayed* `#b1a491`. |
| Value | A 1.2 px accent fill (`#e8692a`) runs along the rail up to the cap. It is drawn in software, so it is exact at every value. |
| States | Normal, hover and drag are separate cap renders. Hover and drag are a slight lift and sheen. |

## 9. LEDs: rendered lens

| LED | Spec |
|---|---|
| Macro LEDs | Glass lens domes, 9 px, at (macro x + 66, 883), 0.75 mm high, in each macro's colour. Lit: emission 1.4. Unlit: a dim tinted lens (0.12). The macro's on-condition is unchanged. |
| ARP LED | An 11 px lens at (1324.5, 930) in a recessed slot, graphite `#5a5752` when off and the ARP colour when lit. |

## 10. Knobs

| Knob | Change |
|---|---|
| Macro knob | **Unchanged.** It is the approved Blender sprite, and the reference for all the light above. |
| Layer knobs | The cap and rings are unchanged. The **names are lifted** so they sit as far above the tick dots as the values sit below (lift 15.1 design px). Done, commit e5d62e2. |
| Mini and ADSR knobs | Unchanged (vector) |

## 11. Colour summary (displayed)

| Surface | Colour |
|---|---|
| Backdrop | `#b7a89c` |
| Teak | `#c28c68` / `#b98664` |
| Housing | `#e9e2d4` → `#e0d9cb` |
| Cards | `#fcf5eb` → `#f7f0e5` |
| Macro panel | `#f3ede1` → `#f2ebdf` |
| Keybed floor | `#efeadf` |
| Key, normal / on | `#f2ece4` / `#f8e0cf` |
| ARP card | `#ede7de` |
| Advanced | `#efe9e0` |
| Fader cap / rail | `#c8c4bd` / `#b1a491` |
| Accent | `#e8692a` (unchanged) |
| Text | `#23211e` (unchanged) |

## 12. How it is built

> **Built.** The production skin is `apps/plugin/Source/CeramicSkin` with assets from
> `tools/blender-assets/skin` (see docs/architecture.md, "Ceramic skin"). Ceramic is the
> default look; Cmd+Option+G or `OSP_SKIN=classic` shows the classic drawing for
> comparison.

The layout is adaptive: the ARP panel adds 187 px, and the two- and three-layer card
widths differ. So the build does not use one full-window bitmap. It uses offline-rendered
parts drawn by a skin:

- **9-slice frames:** housing, card, bezel lip, macro panel, keybed recess.
- **Sprites:**
  - A1 keys per state and per radius;
  - three badges;
  - the C1 cap in three states;
  - the T3 rail as a 3-slice;
  - lens LEDs per colour, lit and unlit;
  - the teak 3-slice.
- **Scales:** assets at 1× and 2×, area-resampled for other display scales and cached per
  scale. Nothing is decoded or scaled on the audio thread.
- **A/B switch:** the classic look stays available, so the two can be compared, and host
  state stays compatible.
