# Art Direction Gate R2: style-frame sheets

Generated from `blender/cine/style/specs/*.json` (written by `make_style.py`) by `style_report.py`.

**Coordinates:** metres in the product frame, measured from the instrument's centre at rest:
- x runs along the keys, to the right;
- y runs toward the back;
- z is up.

The Monument family stands the instrument on the walnut floor; in those frames the top surface is near z = −0.086.

**Renders:** 1920 × 1080, 128 samples, noise threshold 0.01, 16-bit PNG.

## 01 · SF_A1: A Monument, Diagonal monolith

> The instrument as a monolith on dark walnut: seen from its front-left corner and turned 14 degrees, its long edge becomes one strong diagonal that climbs from the lower left into the dark. The timber cheek anchors the frame; the far end dissolves. The wordmark stands in the darkness beyond, and the instrument's far edge cuts across its feet: the type is behind the object, not above it.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 50 mm, f/8; visible width at the target 0.52 m (instrument 0.40 m) |
| Camera | position [-0.3701, -0.5006, 0.1659], target [0.06, 0.02, -0.085], focus [-0.06, -0.05, -0.085], shift [0, -0.06] |
| Product | translate [0, 0, -0.098]; turn 14 |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Fill, Front kicker, Floor overhead |
| Setup light Floor rake | energy × 0.45 |
| Setup light Top line | energy × 0.35 |
| Light: Monument sweep | 1.4 × 0.3 m, 22 W, at [0.75, 0.35, 0.7] aimed at [0, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | +0 EV / × 1 / × 0.9 |

## 02 · SF_A2: A Monument, Plinth from above

> The plinth view: from above and to the right, the instrument floats as a single bright plane against the walnut and the shadowed floor beyond, its diagonal running against the planks. The quietest of the three, and the closest to a classic hero, but with mass and darkness on both sides.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 65 mm, f/9; visible width at the target 0.58 m (instrument 0.40 m) |
| Camera | position [0.5359, -0.686, 0.4972], target [-0.01, 0, -0.09] |
| Product | translate [0, 0, -0.098] |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Fill, Front kicker, Floor overhead |
| Setup light Floor rake | energy × 0.45 |
| Setup light Top line | energy × 0.35 |
| Light: Monument sweep | 1.4 × 0.3 m, 22 W, at [0.75, 0.35, 0.7] aimed at [0, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | +0 EV / × 1 / × 0.9 |

## 03 · SF_A3: A Monument, Corner monument

> Confident scale: the camera stands over the front-right corner, so the ARP card, the modulation display and the third source card become architecture and the instrument fills the frame on a falling diagonal. A single side key models the controls; the far end and the floor go to black.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 50 mm, f/8; visible width at the target 0.41 m (instrument 0.40 m) |
| Camera | position [0.37, -0.3443, 0.1461], target [0.02, 0.03, -0.09], focus [0.13, -0.08, -0.086], shift [0, -0.04] |
| Product | translate [0, 0, -0.098]; turn -10 |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Fill, Front kicker, Floor overhead |
| Setup light Floor rake | energy × 0 |
| Setup light Top line | energy × 0.35 |
| Setup light Key softbox | energy × 0.35 |
| Light: Side key | 0.35 × 0.25 m, 9 W, at [0.55, -0.2, 0.12] aimed at [0.1, -0.02, -0.09], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | +0 EV / × 1 / × 0.9 |

## 04 · SF_B1: B Sculpture, Ivory three-quarter

> The ivory portrait: a seamless warm cyclorama (no horizon line), one large low key from behind left so the ceramic turns from light to shade across its width, a black negative-fill card on the right to keep the right cheek in shadow, and a long soft shadow toward the viewer.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 85 mm, f/11; visible width at the target 0.47 m (instrument 0.40 m) |
| Camera | position [-0.5168, -0.8952, 0.3762], target [0.02, 0, 0], shift [-0.04, -0.02] |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Key | 1.8 × 1 m, 55 W, at [-0.9, 0.7, 0.45] aimed at [0, 0, 0] |
| Light: Edge strip | 2 × 0.1 m, 26 W, at [0.85, 0.55, 0.3] aimed at [0, 0, 0], spread 90°, lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Flag: Negative fill | black, 1.2 × 0.8 m, at [0.75, -0.35, 0.15]; absorbs bounce (negative fill) |
| Card: Sweep | 1.8 × 0.3 m, strength 30, its highlight placed on [0, -0.05, 0.012]; the instrument only |
| Exposure / world / displays | -0.45 EV / × 0.15 / × 1 |

## 05 · SF_B2: B Sculpture, Low profile, the curve

> The curve: low over the right end, along the instrument's length. Every key front, knob skirt and the rounded ceramic edge catches the light from behind; the cyclorama falls from dark to warm grey behind it, so the silhouette reads against a gradient, not a line.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 85 mm, f/11; visible width at the target 0.32 m (instrument 0.40 m) |
| Camera | position [0.5973, -0.3732, 0.1497], target [-0.03, 0, 0] |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Key | 2.2 × 0.9 m, 45 W, at [0.1, 0.95, 0.85] aimed at [0, 0, 0] |
| Light: Top strip | 2.2 × 0.12 m, 10 W, at [0.3, -0.1, 0.75] aimed at [0, 0, 0], spread 70°, lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Flag: Negative fill | black, 1.4 × 0.6 m, at [0.55, -0.55, 0.12]; absorbs bounce (negative fill) |
| Card: Sweep | 1.8 × 0.15 m, strength 40, its highlight placed on [0, -0.06, 0.012]; the instrument only |
| Exposure / world / displays | -0.35 EV / × 0.15 / × 1 |

## 06 · SF_B3: B Sculpture, Overhead, long light

> From above, turned off-axis: a single low window light from the left lays a long, soft shadow across the cyclorama, the instrument becomes a graphic object in a field of warm negative space. The displays are flagged so the waveforms read clean.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 85 mm, f/11; visible width at the target 0.68 m (instrument 0.40 m) |
| Camera | position [-0.0762, -0.5419, 1.5035], target [0.06, -0.02, 0], shift [-0.08, 0] |
| Product | turn 22 |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Window | 1 × 1.6 m, 120 W, at [-1.3, 0.4, 0.55] aimed at [0, 0, 0] |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Flag: Negative fill | black, 1.2 × 1.2 m, at [0.65, -0.1, 0.35]; absorbs bounce (negative fill) |
| Exposure / world / displays | -0.25 EV / × 0.15 / × 1 |

## 07 · SF_C1: C Silhouette, Timber, keys and macros

> After R02 A: almost everything is darkness. One narrow strip skims the front-left corner: the walnut cheek, the PITCH and MOD strips, the first macro knobs and the keys catch it; the rest of the instrument is suggested, not shown.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 85 mm, f/8; visible width at the target 0.20 m (instrument 0.40 m) |
| Camera | position [-0.4, -0.38, 0.16], target [-0.1, -0.05, -0.005], focus [-0.17, -0.1, 0] |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Key softbox, Top line, Front kicker, Fill, Floor overhead, Floor rake |
| Setup light Rim strip | energy × 0.25 |
| Light: Strip | 0.03 × 0.6 m, 0.8 W, at [-0.32, -0.4, 0.12] aimed at [-0.12, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | -1.3 EV / × 0.05 / × 0.35 |

## 08 · SF_C2: C Silhouette, A pool of light on the controls

> A single pool of light falls on the macro row and the keys; the displays glow faintly beyond it, the rest is black. The pool is the subject: the instrument is played where the light is.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 85 mm, f/8; visible width at the target 0.34 m (instrument 0.40 m) |
| Camera | position [0.1278, -0.5973, 0.5557], target [-0.04, -0.03, 0] |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Key softbox, Rim strip, Top line, Front kicker, Fill, Floor overhead, Floor rake |
| Light: Pool | 0.1 × 0.1 m, 1 W, at [-0.08, -0.08, 0.7] aimed at [-0.06, -0.06, 0], spread 14°, lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | +0 EV / × 0.03 / × 0.25 |

## 09 · SF_C3: C Silhouette, The outline

> The outline: only a rim behind the instrument. The knobs' silhouettes along the top edge and a hairline along the ceramic are enough to recognise it; below it, darkness as negative space.

| | |
|---|---|
| Base | ANDOR_FLOATING (lights and materials of shot B1_R4); 16:9 |
| Lens | 85 mm, f/9; visible width at the target 0.32 m (instrument 0.40 m) |
| Camera | position [-0.3468, -0.6522, 0.1302], target [0, 0, 0], shift [0, -0.08] |
| Stage | the dark walnut theatre (ANDOR FLOATING's floor) |
| Setup lights off | Key softbox, Top line, Front kicker, Fill, Floor overhead, Floor rake |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | -0.2 EV / × 0.03 / × 0.08 |

## 10 · SF_D1: D Immersion, The three sound engines

> The three sound engines as a landscape: the camera travels low over the source cards, the near knobs soft in the foreground, the middle display sharp, the far one receding. A low raking light draws every knob's shadow across the ceramic.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 50 mm, f/5.6; visible width at the target 0.21 m (instrument 0.40 m) |
| Camera | position [-0.14, -0.15, 0.1], target [0.03, 0.06, 0], focus [0, 0.06, 0.01] |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Rake | 0.9 × 0.15 m, 60 W, at [-0.9, 0.55, 0.13] aimed at [0, 0, 0] |
| Light: Sky | 1.2 × 1.2 m, 3 W, at [0.6, -0.6, 0.5] aimed at [0, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | -0.3 EV / × 0.1 / × 1 |

## 11 · SF_D2: D Immersion, The macro row

> The macro row as architecture: six knobs recede on a diagonal, each throwing a long evening shadow across the panel; the keys run beneath them out of focus. Close enough to be inside the instrument, far enough to read what it is.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 85 mm, f/5.6; visible width at the target 0.13 m (instrument 0.40 m) |
| Camera | position [0.14, -0.27, 0.07], target [-0.07, -0.06, 0], focus [-0.04, -0.065, 0.01] |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Rake | 0.9 × 0.15 m, 60 W, at [-0.9, 0.55, 0.13] aimed at [0, 0, 0] |
| Light: Sky | 1.2 × 1.2 m, 3 W, at [0.6, -0.6, 0.5] aimed at [0, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | -0.3 EV / × 0.1 / × 1 |

## 12 · SF_D3: D Immersion, Keys and expression

> From the left end, along the instrument: the walnut cheek, the PITCH and MOD strips and the keyboard in the foreground, the macro row and the source cards rising behind into darkness. The main surface extends away from us like a street.

| | |
|---|---|
| Base | ANDOR_GALLERY (lights and materials of shot A2); 16:9 |
| Lens | 50 mm, f/8; visible width at the target 0.30 m (instrument 0.40 m) |
| Camera | position [-0.36, -0.24, 0.075], target [0.02, -0.07, 0], focus [-0.13, -0.115, 0.004] |
| Stage | seamless cyclorama: colour [0.6, 0.565, 0.52], cove radius 1.4 m, wall 2.2 m behind (no horizon line) |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Light: Rake | 0.9 × 0.15 m, 60 W, at [-0.9, 0.55, 0.13] aimed at [0, 0, 0] |
| Light: Sky | 1.2 × 1.2 m, 3 W, at [0.6, -0.6, 0.5] aimed at [0, 0, 0], lights the product only |
| Flag: Display flag | black, 0.9 × 0.6 m, where the display glass reflects the camera; seen only in reflections |
| Exposure / world / displays | -0.3 EV / × 0.1 / × 1 |

