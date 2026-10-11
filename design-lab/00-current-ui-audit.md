# 00 — Current UI audit (baseline for the design lab)

Captured from the production code at commit `301e88a` (branch `claude/charming-lovelace-n25fte`).
Nothing in the production tree was changed for this lab. The one helper,
`design-lab/scripts/DesignLabExport.inc`, is a hidden Catch2 test (`[.][design-lab]`). It is
compiled only when `#include "../../design-lab/scripts/DesignLabExport.inc"` is appended
temporarily to `tests/plugin/PluginProcessorTests.cpp`, and that line is then reverted.
See the README.

## Immutable baseline

| What | Where |
|---|---|
| The production editor, as the plugin draws it | `references/current-gui/export{1x,1.5x,2x,4x}/full@*.png` |
| Every component's own paint, transparent, in z-order | `references/current-gui/export*/layers/NNNN.png` |
| Bounds, class, z-order, slider and button state of each component | `references/current-gui/export*/layers.json` (schemaVersion 1) |

**Export state** (the reference screenshot's setup):
- Layers: A, a vowel source in One Shot; B, a pluck in Granular; C, a pluck at 100 % REIMAGINED.
- Macros: LIFE 72, DRIVE 46, CHARACTER 42, MOVEMENT 28, ECHO 64, SPACE 18.
- One route, LFO 1 → CHARACTER, so a modulation halo shows.
- Knob skin: `macros` (the approved Blender knob on all six).
- Editor size: 1448 × 1086.

The export draws through `Component::createComponentSnapshot`, the plugin's own renderer, at
1×, 1.5×, 2× and 4×. Text and vectors are drawn at each scale; nothing is upscaled.

## Dimensions and layout (design px, `apps/plugin/Source/Design.h` → `layout`)

| Element | Rect (x, y, w, h) | Corner radius |
|---|---|---|
| Editor (canvas) | 0, 0, 1448, 1086 | — |
| Housing | 25, 19, 1398, 1053 | 26 |
| Preset bar | 501, 45, 446, 48 | ≈ 12 |
| Volume slider | 1150, 47, 194, 44 (track at y = 80, x 1157–1337; thumb 12 px disc) | — |
| Header menu | 1366, 50, 28, 36 | — |
| Engine cards A / B / C | 35 / 498 / 961, 127, 451, 525 | 16 |
| Source displays | card + (16, 63), 419 × 258 | 9 |
| Layer badge | card + (21, 11), 47 × 42 | ≈ 7 |
| M / S layer-state buttons | card + (77, 10 / 32), 27 × 21 | 4 |
| EQ button | card + (215, 16), 38 × 33 | ≈ 6 |
| Mode selector | card + (261, 12), 128 × 41 | 0.16 h ≈ 7 |
| Layer knobs (5 per card) | card + (22 + 80 i, 339), 87 × 100 (dial 75 × 74) | — |
| Modifier buttons (4 per card) | card + (16 + 107 i, 459), 98 × 45 | 7 |
| Macro panel | 35, 682, 1378, 216 | 13 |
| Macro knobs (sprite) | x = 62 + 146 i, y = 752, 132 × 132 (ECHO and SPACE swap places in z-order) | — |
| Macro LEDs | (128 + 146 i, 883), 9 px | — |
| Divider (macros / envelope) | x ≈ 945, y 695–885, 1 px | — |
| Modulation tabs strip | 955, 688, 450 × 24 (5 tabs today: AMP, ENV 1, ENV 2, LFO 1, LFO 2) | — |
| Envelope display | 967, 714, 426 × 90 | 12 |
| ADSR mini knobs | 973 + 108 i, 806, 90 × 87 | — |
| Pitch / mod wheels | 42 / 93, 913, 36 × 115 (incl. label) | — |
| Keyboard | 148, 913, 1150 × 101; keybed tray 146, 910, 1154 × 106 | ≈ 6 |
| ARP card | 1308, 913, 105 × 64; inner pattern pill (7, 34, 90, 22) | 10 / 9 |
| Advanced button | 1308, 983, 105 × 31 | 0.22 h ≈ 7 |
| Status line | 48, 1038, 600 × 24 | — |

## Materials and colours (`Design.h` → `colour`)

| Token | Value | Use |
|---|---|---|
| backdropTop / Bottom | #d6ccc2 / #e3dacd | Surround outside the housing |
| housingTop / Bottom / Rim | #eae3d6 / #ded5c8 / #c9bfb1 | Main body (described as "powder-coated") |
| cardTop / Bottom | #f5eee4 / #e9e2d6 | Engine cards |
| panelTop / Bottom | #f2ebe0 / #e6dfd2 | Macro panel, ARP card |
| buttonTop / Bottom | #f8f4ec / #e9e3d8 | All `draw::button` keys |
| edgeLight / edgeShade / hairline / divider | #fffbf2 / #c8c0b2 / #d8d0c3 / #cbc2b4 | Painted edges |
| wellA / wellB / wellRim / wellGrid | #1f2324 / #1c2325 / #111415 / #2e3434 | Waveform and envelope displays |
| text / textSecondary / textMicro | #23211e / #5d5850 / #8a847b | Typography |
| accent (Top / Bottom / Soft) | #e8692a (#f36a2c / #d3461a / #f2c8b0) | Active states, underline, value fills |
| macro colours | LIFE #be7552, DRIVE #b5843a, CHARACTER #7b8d69, MOVEMENT #73899c, ECHO #5c9490, SPACE #96839d | Halos, LEDs, popovers |
| layer identities | A orange, B blue-grey, C sage (badge, LED, arc, thumb, wave) | Cards |
| knob cap (vector knobs) | #f8f3ea → #d9d0c2, rim #b9ad9b, pointer #141210 | Layer and mini knobs |

**Typefaces:**
- Inter (Light, Regular, Medium, SemiBold, Bold, ExtraBold) for all UI text.
- Outfit (ExtraBold, ExtraLight) for the ANDOR/OSP wordmark.

**Texture usage:** none. Every surface is a two-stop linear gradient. The only image asset is the approved knob's sprite packs (`apps/plugin/Resources/knobs/macro-ivory`).

## Depth: shadows, edges and states (as painted today)

| Element | How it is drawn (`Design.cpp`) |
|---|---|
| Housing | Two cached blurred shadows: 26 px at alpha 0x2a, offset (0, 10); then 4 px at 0x22, offset (0, 2). A white rim fades in over 6 px at the top and 5 px at the left. A 1 px rim at 55 %. |
| Raised panels and cards (`draw::raised`) | 9 px shadow (0x1e, offset 3) and 2 px shadow (0x1c, offset 1). A 1.2 px light top edge, a 1 px shade bottom edge, a 0.8 px hairline. |
| Displays (`draw::well`) | A 1 px light lip below. A 13 px inner shade from the top (55 % black → 0), 7 px and 6 px side shades. A faint lower reflection. A 1.2 px dark rim. |
| Buttons (`draw::button`) | 3 px shadow. **Normal:** a #f8f4ec → #e9e3d8 gradient, a 1 px white lit edge, a 1 px #cbc2b4 outline. **Hover:** the top stop 20 % brighter. **On:** a warm #f1e3d6 face, a 30 %-height inner shade and an accent outline at 70 %. **Pressed:** drawn as on. **Disabled:** there is no separate state; the icon is drawn lighter (B's loop key). |
| LEDs (`draw::led`) | A glossy bead: radial gradient, white highlight, optional halo (lit = modulated). |
| Volume slider | A 1.5 px track (#bdb2a2) with a light lip; the value part is warmer; a 12 px cream disc thumb with a 3 px shadow. |
| Modulation tabs | Words only; the selected tab has an accent underline, and each source has a small socket ring. No surface. |

## Visual inconsistencies found

1. **Two realities.** The macro knob is a path-traced object with a real contact shadow and a soft studio key. Everything else is a two-stop gradient with painted 1 px edges and blurred-shape shadows. Next to the knob, the cards and buttons read as flat illustrations.
2. **Light direction disagrees.** The knob's key comes from the upper left (az 128°, el 70°). The painted edges are lit straight from the top, and the housing rim from the top and the left. The shadows fall straight down (offset y only).
3. **Edges are lines, not forms.** Every raised element gets the same 1 px light, 1 px shade and hairline, whatever its size. A 27 px M/S key and a 451 px card have identical edges.
4. **Active is a colour, not a state.** An active modifier is a pink fill plus an orange outline, with no depth change. Pressed and active look identical.
5. **Disabled has no treatment.** The greyed loop on card B in granular mode is only a lighter icon.
6. **The volume thumb is a different object.** A 12 px disc is neither a fader cap nor a knob, and it is the only control without a family.
7. **The LEDs are glossy beads.** Their highlight implies a different light than the knob's.
8. **The keybed and displays are painted recesses.** Their inner shades are fixed pixel gradients, so their depth does not scale with size.
9. **Tabs.** Five tabs today (AMP, ENV 1, ENV 2, LFO 1, LFO 2). The queued MOD-FINAL spec moves to six (AMP | ENV | LFO 1 | LFO 2 | VEL | NOTE). The lab keeps today's five, because the layout must not change. Every tab family here is width-agnostic.

## Existing Blender pipeline

- `tools/blender-assets/` is the approved knob's generator:
  - geometry: a procedural revolved profile;
  - three ivory finishes;
  - the `studio-soft` rig: key, fill and edge area lights, plus a warm world;
  - a top-down orthographic camera;
  - iterative exposure calibration;
  - 129 deterministic frames, packed as 176 / 352 px sprite packs.
- Runs headless through the `bpy` 4.2 Python module (Blender downloads are blocked here); Cycles on CPU, about 0.7 Msamples/s on 4 cores.
- The knob's colour management: Standard view, Look None, exposure from calibration, gamma 1, sRGB display, 8-bit straight-alpha RGBA PNG, transparent film.

## Approved knob

- Source: `tools/blender-assets/config/macro-knob.json` (Satin Ivory, engraved line).
- Packs: `apps/plugin/Resources/knobs/macro-ivory/`.
- Report: `docs/reports/knob-phase1.md`.
- The lab never re-renders it. Every full-GUI comparison shows it exactly as the plugin draws it, taken from the layer export: the same sprites, the same halos, the same values.

## UI Mother — Eclyde kit

- **Not accessible.** No purchased source files are in the repository or this environment.
- The only references are the four Eclyde preview images sent in the conversation:
  - dark UI;
  - brushed gold and aluminium knobs;
  - amber glowing pads;
  - recessed sliders with an orange fill.
- They informed:
  - A2's lit groove ("clean active-state illumination");
  - the T2 recessed slot with a software value fill;
  - the C2 metal cap.
- No kit asset is copied, traced or redistributed (`references/uimother/README.md`).
