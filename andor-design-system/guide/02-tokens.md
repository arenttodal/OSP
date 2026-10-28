# 02 — Tokens: colour, type, geometry, material

Every value here is **read from the code**, not restated from memory:
- `shared/andor-ui/Source/Design.h` (`design::colour`, `design::layout`, `design::draw`);
- `shared/andor-ui/Source/OspLookAndFeel.h` (`palette`, `fonts`, `type`).

If this page and the code ever disagree, the code wins and this page is wrong.

These are ANDOR OSP's values. A new ANDOR product uses them unchanged, by including the
headers. It never copies them (see HANDOFF.md, rules 1–4).

## Canvas

| Token | Value | Meaning |
|---|---|---|
| `design::width` × `design::height` | **1448 × 1086** reference px | The whole instrument is laid out once, in these coordinates. It is scaled as one piece to the window (`frame::fit`). |
| Window width | **869 – 1810 px**, default **1086** | Fixed aspect ratio. The height follows the instrument (`frame::applyWindowShape`). |
| `layout::arpShift` | **187 px** | The arpeggiator's inline editor pushes the keyboard row down. The window grows by this much. |

## Colour: the housing and its materials (`design::colour`)

| Token | ARGB | Use |
|---|---|---|
| `backdropTop` / `backdropBottom` | `ffd6ccc2` / `ffe3dacd` | The surround outside the housing |
| `housingTop` / `housingBottom` | `ffeae3d6` / `ffded5c8` | The instrument's body |
| `housingRim` | `ffc9bfb1` | Housing edge |
| `cardTop` / `cardBottom` | `fff5eee4` / `ffe9e2d6` | Source cards (lit top, warmer foot) |
| `panelTop` / `panelBottom` | `fff2ebe0` / `ffe6dfd2` | The macro + envelope panel |
| `edgeLight` / `edgeShade` | `fffffbf2` / `ffc8c0b2` | The 1 px lit top edge and shaded bottom edge of raised parts |
| `hairline` / `divider` | `ffd8d0c3` / `ffcbc2b4` | Rules and the macro/envelope divider |
| `wellA` / `wellB` / `wellRim` | `ff1f2324` / `ff1c2325` / `ff111415` | Dark displays (waveforms, envelope, popover wells) |
| `wellGrid` / `wellText` | `ff2e3434` / `ff8d8c86` | Grid and text on dark displays |
| `text` / `textSecondary` / `textMicro` | `ff23211e` / `ff5d5850` / `ff8a847b` | The three text tones. Hierarchy comes from these, size and tracking, never from bold. |
| `accent` (+ `accentTop` `fff36a2c`, `accentBottom` `ffd3461a`, `accentSoft` `fff2c8b0`) | `ffe8692a` | The one live accent: active states, value arcs, held keys |
| `arp` / `arpOnDark` | `ffc98f3e` / `ffe9b45f` | The arpeggiator's amber (its light and steps only) |
| `knobCapTop` / `knobCapBottom` / `knobRim` / `knobTrack` / `knobPointer` / `tick` | `fff8f3ea` / `ffd9d0c2` / `ffb9ad9b` / `ffcfc6b8` / `ff141210` / `ff4a4640` | The drawn knob (where no rendered sprite is used) |
| `buttonTop` / `buttonBottom` | `fff8f4ec` / `ffe9e3d8` | Drawn button faces |
| `whiteKeyTop` / `whiteKeyBottom` / `blackKeyTop` / `blackKeyBottom` / `keyLine` | `fffbf8f2` / `ffefe9de` / `ff3a3936` / `ff1e1e1d` / `ffd2cabd` | The keyboard |

### The six macro identities (`colour::macro (i)`, data order)

All six sit at CIE L* 56–58. They mark only three things:
- a macro's thin value ring;
- its light;
- its popover's accents.

| # | Macro (OSP) | ARGB | Name |
|---|---|---|---|
| 0 | LIFE | `ffbe7552` | rust |
| 1 | DRIVE | `ffb5843a` | ochre |
| 2 | CHARACTER | `ff7b8d69` | moss |
| 3 | MOVEMENT | `ff73899c` | mineral blue |
| 4 | SPACE | `ff96839d` | muted mauve |
| 5 | ECHO | `ff5c9490` | verdigris |

On screen, ECHO (5) sits **before** SPACE (4): the knob centres are 128, 274, 420, 566, 858 and 712 px.

A product renames the macros (`ProductInfo::macros`). It keeps the colour of each position.

### Layer identities (`colour::identity (layer)`)

| Layer | badge top / bottom | led | arc | thumb | wave |
|---|---|---|---|---|---|
| A (amber/terracotta) | `ffe9844a` / `ffa9420e` | `fffc9f4f` | `ffe8692a` | `ffef8640` | `ffeeb070` |
| B (slate-blue) | `ff8297aa` / `ff324659` | `ff8cc4ef` | `ff5d93c4` | `ff69a9d5` | `ffb4c6dc` |
| C (sage) | `ff9db4a0` / `ff4b6b55` | `ffa9d6ae` | `ff6f9e7a` | `ff7fb38a` | `ffbad3b9` |

REIMAGINED has its own continuum (`colour::reimagined (0..1)`). It runs rust → amber → muted gold → moss → mineral teal → dusty blue → mauve, interpolated in OKLab. It is the same on every layer.

### Interface palette (`palette`, OspLookAndFeel.h)

| Token | ARGB | Token | ARGB |
|---|---|---|---|
| `housing` | `fff2f0ea` | `raised` | `fff8f7f3` |
| `recessed` | `ffe8e6df` | `hairline` | `ffd5d1c8` |
| `text` | `ff272622` | `textDim` | `ff77746d` |
| `graphite` | `ff34332f` | `accent` | `ffe1774f` |
| `amber` | `ffd79a43` | `gold` | `ffdab66d` |
| `coral` | `ffd47a62` | `rose` | `ffc48791` |
| `lavender` | `ffaaa0bb` | `mineral` | `ff829cac` |
| `sage` | `ff8fa898` | `frame` | `ffe7e4dc` |
| `displayLine` | `ff4a4844` | `displayText` | `ffa29e95` |
| `valueBubbleText` | `fff4f1ea` | `wave` | `ffded7c8` |
| `ivory` (white keys) | `fffbfaf6` | `ebony` (black keys) | `ff3b3a36` |

## Type (`fonts`, `type`)

There are two families, both embedded (SIL Open Font License):
- **Inter** (Light, Regular, Medium, SemiBold, Bold, ExtraBold) for the interface. Its tabular figures are the default, so values do not jump.
- **Outfit** (ExtraBold = `displayBold`, ExtraLight = `displayLight`) for the identity only.

| Role (`type::`) | Size px | Weight | Tracking |
|---|---|---|---|
| Identity mark "ANDOR" | 60 (×1.07 wide) | Outfit ExtraBold | 0 |
| Identity product "/OSP" | 60 (×1.07 wide) | Outfit ExtraLight | 0 |
| Tagline ("One Shot Performer") | 15 | Inter Regular | 0.16 |
| `preset` | 22·k | Regular | 0.01 |
| `sourceRoot` | 22 | Medium | 0 |
| `sourceFilename` | 15 | Regular | 0.01 |
| `sourceMode` | 16.5·k | Regular | 0.02 |
| `controlLabel` | 14.5·k | Medium | 0.05 |
| `controlValue` | 17·k | Medium | 0 |
| `macroLabel (h)` | h | Medium | 0.05 |
| `panelHeader` | 16 | SemiBold | 0.05 |
| `sectionTitle` | 23.5 | SemiBold | 0.05 |
| `trackWord` | 14 | Medium | 0.11 |
| `micro (h=15.5)` | h | Regular | 0.01 |
| `button (h)` | h | Medium | 0.02 |
| `popupTitle (h=13)` | h | Regular | 0.12 |
| `popupMode (h)` | h | Regular | 0.06 |
| `popupLabel (h)` | h | Medium if h < 12, else Regular | 0.05 |
| `popupValue (h)` | h | Medium | 0 |
| `annotation (h)` | h | Regular if h < 11, else Light | 0.04 |
| Status line (`frame::setUpStatusLabel`) | 16 | `micro` | `textMicro` darkened 15 % |
| Keyboard C labels | min(14, key × 0.6) | Regular | 0.04 |

Names are written as the product writes them: macro names in capitals, the tagline as a sentence.

## Geometry (`design::layout`, reference px)

| Part | Rectangle (x, y, w, h) | Radius |
|---|---|---|
| Housing | 25, 19, 1398, 1053 | 26 |
| Identity baseline | (51, 79); tagline box 51, 88, 300, 14 | — |
| Preset bar | 501, 45, 394, 48 | — |
| Save preset key | 903, 45, 44, 48 | — |
| Header MIX (2–3 layers) | 966, 23, 164, 88 | — |
| Volume | 1150, 47, 194, 44 | — |
| Menu (three dots) | 1366, 50, 28, 36 | — |
| Sources (1–3 cards) | 35, 127, 1379, 525. Gap between cards 12 (`frame::sourceColumn`) | card 16 |
| Macro + envelope panel | 35, 682, 1378, 216; envelope from x = 955; divider at x 945, y 695, h 190 | panel 13 |
| Macro name / knob | 150 × 22 centred at (cx, 719) / 132 × 132 centred at (cx, 818); light 9 px at knob centre + 65 | — |
| Modulation panel | 955, 688, 450, 206 | — |
| Pitch / mod wheel | 42 / 93, 913, 36 × 115 | — |
| Keyboard | 148, 913, 1150, 101. 88 keys (A0–C8), 52 white keys, black keys 62 % long, keybed 2 px left/right, 3 above, 2 below | 3.5 (white key foot) |
| Status line | 48, 1038, 600, 24 | — |
| ARP card / Advanced key | 1308, 913, 105 × 64 / 1308, 983, 105 × 31 | — |
| ARP inline editor | 35, 908, 1378, 177 | — |

## Material (`design::draw`, `skin::`)

The **ceramic** skin is the look (`skin::Look::ceramic`). Its surfaces are offline Blender renders: the housing with its teak rails, the cards, the bezels, the keys in four states, the badges, the fader, the lenses and the keybed. They are drawn as nine-slices at their rendered places (`skin::Pieces`). See 06-assets.md.

On top of them the code draws:
- **Housing:** `draw::housing`, the body with a broad quiet shadow on the surround.
- **Raised:** `draw::raised`, a soft contact shadow, vertical light and a 1 px lit top edge. Ceramic draws `raisedChildren`.
- **Well:** `draw::well`, a recessed dark display with an inner shadow under its top edge and a faint lower reflection.
- **Knob:** `draw::knob`, with:
  - 9 tick dots at 1.37 body radii;
  - a track and value arc at 1.13;
  - a thin flat ring of the macro's colour on macros (`thinArc` 2.4);
  - a pointer from 0.36 to 0.8 radii;
  - a travel from 1.25π to 2.75π.
  The macros wear the rendered **ivory cap** (`macro-ivory` sprites, see 06-assets.md).
- **Light:** `draw::led`. In ceramic, a rendered glass lens per macro (`led-<macro>-lit/unlit`) with a drawn glow when lit.
- **Disabled:** the look-and-feel's dimmed state (`enabled = false`), and 0.4 alpha for a control the product's engine does not read. This is ANDOR OSP's existing disabled treatment.
