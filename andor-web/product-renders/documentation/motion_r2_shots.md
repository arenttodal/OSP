# Motion Gate R2: shot sheets

Generated from `blender/cine/shots/*.json` by `gate_report.py`.

**Coordinates:** metres in the product frame, from the instrument's centre at rest:
- x along the keys, to the right;
- y toward the back;
- z up.

**Visible width:** at distance d with focal length L, the visible width is 36·d/L (36 mm sensor).

**Times** are seconds into the move. Every shot has a 1 s handle at each end that holds the end pose.

## R01_MON: R01 Grand Reveal, Monument

> Darkness, and the corner of the instrument close by: a rim of light on the teak cheek, a strip skimming PITCH, MOD and the first keys - C1's world. The light arrives first: the key rises across the ceramic, the long top sweep follows. Only then does the camera begin to withdraw, climb and travel right while the instrument turns on its own axis toward its rest, so the depth never flattens. The wordmark waits in the dark behind it and is uncovered by that coordinated movement; the displays wake after the light. In the last seconds the complete instrument settles in A2's three-quarter, the wordmark resolved beyond its far corner.

| | |
|---|---|
| Setup | ANDOR_FLOATING (base light rig of shot B1_R4); 16:9, 24 fps, 12 s + 1 s handles |
| Lens | 65 (constant) mm, f/9 |
| Camera position | 0 s: [-0.135, -0.427, 0.036] (hold) → 1.6 s: [-0.135, -0.427, 0.036] (bezier [0.62, 0, 0.3, 1]) → 11.6 s: [0.5359, -0.686, 0.4972] |
| Camera target | 0 s: [-0.062, -0.093, -0.088] (hold) → 1.6 s: [-0.062, -0.093, -0.088] (bezier [0.62, 0, 0.3, 1]) → 11.6 s: [-0.01, 0, -0.09] |
| Focus point | 0 s: [-0.1, -0.15, -0.088] (hold) → 1.6 s: [-0.1, -0.15, -0.088] (bezier [0.62, 0, 0.3, 1]) → 11.6 s: [-0.01, 0, -0.09] |
| Visible width (at target) | open 0.20 m → close 0.58 m (instrument: 0.40 m) |
| Product turn | 0 s: 30 (hold) → 2.2 s: 30 (bezier [0.4, 0, 0.25, 1]) → 11.8 s: 0 ° |
| Product translate | [0, 0, -0.098] (constant) |
| Light: Rim strip | energy 0 s: 0.08 (bezier [0.5, 0, 0.4, 1]) → 5 s: 1 |
| Light: Corner strip | energy 0 s: 1 (hold) → 3 s: 1 (smooth) → 8 s: 0 |
| Light: Key softbox | energy 0 s: 0 (hold) → 0.8 s: 0 (bezier [0.5, 0, 0.4, 1]) → 5 s: 1 |
| Light: Monument sweep | energy 0 s: 0 (hold) → 1.5 s: 0 (bezier [0.5, 0, 0.4, 1]) → 6.5 s: 1 |
| Light: Top line | energy 0 s: 0 (hold) → 3 s: 0 (bezier [0.5, 0, 0.4, 1]) → 8.5 s: 0.35 |
| Light: Floor rake | energy 0 s: 0 (hold) → 4 s: 0 (bezier [0.5, 0, 0.4, 1]) → 10.5 s: 0.45 |
| Light: Monument sweep (added) | 1.4 × 0.3 m, 22 W at [0.75, 0.35, 0.7], aimed at [0, 0, 0] |
| Light: Corner strip (added) | 0.03 × 0.6 m, 0.08 W at [-0.2, -0.48, 0.05], aimed at [-0.1, -0.2, -0.09] |
| Setup lights off | Fill, Front kicker, Floor overhead |
| Flags | Display flag (follows the display reflection) |
| World | 0 s: 0.05 (bezier [0.5, 0, 0.4, 1]) → 7 s: 1 |
| Emission | 0 s: 0 (hold) → 3.5 s: 0 (bezier [0.5, 0, 0.4, 1]) → 9 s: 0.9 |
| Typography | separate passes T1, T2, T3 (Outfit ExtraBold wordmark), through the matte; fade and sharpen 4.5–10.5 s |
| Notes | Closing pose = style frame SF_A2 (camera, lens, lights). Each wordmark treatment is its own pass, composited through the instrument's matte on every frame; the clean master carries no type. |

## R02_POOL: R02 Silhouette, The pool of light

> Darkness, a faint rim. A narrow strip of light drifts in from the left and crosses the controls - PITCH and MOD, the keys, the six macros - each catching it in turn. As it passes, a pool begins to gather above the macro row: small and dim at first, widening and warming until the controls sit in one soft circle of light, the labels readable, the displays glowing faintly beyond. The camera only breathes; the light carries the shot, and it is complete only at the end.

| | |
|---|---|
| Setup | ANDOR_FLOATING (base light rig of shot B1_R4); 16:9, 24 fps, 9 s + 1 s handles |
| Lens | 85 (constant) mm, f/8 |
| Camera position | 0 s: [0.1278, -0.5973, 0.5557] (bezier [0.3, 0, 0.7, 1]) → 9 s: [0.1223, -0.5802, 0.5384] |
| Camera target | 0 s: [-0.04, -0.03, 0] |
| Visible width (at target) | open 0.34 m → close 0.33 m (instrument: 0.40 m) |
| Product | stationary |
| Light: Rim strip | energy 0 s: 0.08 (bezier [0.5, 0, 0.4, 1]) → 3 s: 0.15 |
| Light: Strip | energy 0 s: 0 (bezier [0.5, 0, 0.4, 1]) → 1.2 s: 1 (hold) → 5 s: 1 (smooth) → 7.5 s: 0; translate 0 s: [0, 0, 0] (hold) → 0.6 s: [0, 0, 0] (bezier [0.45, 0, 0.55, 1]) → 6.8 s: [0.36, 0, 0] |
| Light: Pool | energy 0 s: 0 (hold) → 3.4 s: 0 (bezier [0.55, 0, 0.35, 1]) → 8.7 s: 1; translate 0 s: [0, 0, -0.14] (hold) → 3.4 s: [0, 0, -0.14] (bezier [0.4, 0, 0.3, 1]) → 8.7 s: [0, 0, 0] |
| Light: Strip (added) | 0.03 × 0.6 m, 0.5 W at [-0.42, -0.3, 0.14], aimed at [-0.2, -0.06, 0] |
| Light: Pool (added) | 0.14 × 0.14 m, 0.6 W at [-0.08, -0.08, 0.7], aimed at [-0.06, -0.06, 0] |
| Setup lights off | Key softbox, Top line, Front kicker, Fill, Floor overhead, Floor rake |
| Flags | Display flag (follows the display reflection) |
| Exposure | 0 (constant) |
| World | 0.03 (constant) |
| Emission | 0 s: 0 (hold) → 5.5 s: 0 (bezier [0.5, 0, 0.4, 1]) → 8.8 s: 0.25 |
| Notes | Closing pose = style frame SF_C2, softened: Pool 0.14 m source, 16 deg spread, 0.6 W (C2: 0.10 m, 14 deg, 1.0 W). The pool rises from 0.56 m to 0.70 m as it brightens, so it widens as it forms. |

## R03_ENG: R03 Above the Sound, Immersive flyover: the three sound engines

> We travel low along the instrument, as if over a landscape at evening: the knobs of the source cards pass close and soft in the foreground, the three displays stand sharp beyond them - A's warm waveform, B's, then C's - while the long shadows of the raking light run with us. The direction of view never changes; only the instrument passes. The third engine arrives as the movement settles.

| | |
|---|---|
| Setup | ANDOR_GALLERY (base light rig of shot A2); 16:9, 24 fps, 10 s + 1 s handles |
| Lens | 50 (constant) mm, f/8 |
| Camera position | 0 s: [-0.25, -0.17, 0.095] (bezier [0.38, 0, 0.62, 1]) → 10 s: [0.12, -0.17, 0.105] |
| Camera target | 0 s: [-0.17, 0.06, 0] (bezier [0.38, 0, 0.62, 1]) → 10 s: [0.2, 0.06, 0] |
| Focus point | 0 s: [-0.15, 0.06, 0.01] (bezier [0.38, 0, 0.62, 1]) → 10 s: [0.15, 0.06, 0.01] |
| Visible width (at target) | open 0.19 m → close 0.19 m (instrument: 0.40 m) |
| Product | stationary |
| Stage | seamless cyclorama (no horizon line) |
| Light: Rake (added) | 0.9 × 0.15 m, 60 W at [-0.9, 0.55, 0.13], aimed at [0, 0, 0] |
| Light: Sky (added) | 1.2 × 1.2 m, 3 W at [0.6, -0.6, 0.5], aimed at [0, 0, 0] |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Flags | Display flag (follows the display reflection) |
| Exposure | -0.3 (constant) |
| World | 0.1 (constant) |
| Notes | Light = style frame SF_D1; f/8 (D1: f/5.6) so more of the functional surface stays readable. The display flag follows the camera every frame. Static displays here; the performance version uses Virtual Studio frames. |

## R04_WIN: R04 The Window Light, B3 coming to life

> The editorial photograph, breathing. From high above, the instrument lies turned in a field of warm light; a window we never see lays its long shadow across the floor. Imperceptibly the camera sinks toward it, and the window light drifts as an afternoon does: the shadow turns a few degrees, the sheen slides across the ceramic and the knobs' small shadows lengthen. Nothing else moves - until, in the performance version, the waveforms and controls begin to play.

| | |
|---|---|
| Setup | ANDOR_GALLERY (base light rig of shot A2); 16:9, 24 fps, 9 s + 1 s handles |
| Lens | 85 (constant) mm, f/11 |
| Camera position | 0 s: [-0.0762, -0.5419, 1.5035] (bezier [0.3, 0, 0.7, 1]) → 9 s: [-0.0685, -0.4877, 1.3532] |
| Camera target | 0 s: [0.06, -0.02, 0] |
| Visible width (at target) | open 0.68 m → close 0.61 m (instrument: 0.40 m) |
| Product turn | 22 (constant) ° |
| Light: Window | translate 0 s: [0, -0.1, 0] (linear) → 9 s: [0, 0.14, -0.04]; energy 0 s: 0.95 (linear) → 9 s: 1 |
| Stage | seamless cyclorama (no horizon line) |
| Light: Window (added) | 1 × 1.6 m, 120 W at [-1.3, 0.4, 0.55], aimed at [0, 0, 0] |
| Setup lights off | Key softbox, Edge strip, Overhead, Fill, Front kicker, Low rake |
| Flags | Display flag (follows the display reflection), Negative fill |
| Exposure | -0.25 (constant) |
| World | 0.15 (constant) |
| Notes | Opening pose = style frame SF_B3. The window moves 24 cm along the wall and sinks 4 cm over the shot (linear: a sun does not ease). Displays are flagged and left static for the performance pass. |

