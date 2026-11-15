# Render settings (pass 2)

| Setting | Value |
|---|---|
| Engine | Cycles, CPU (no GPU in this environment), adaptive sampling (threshold 0.012), OpenImageDenoise |
| Samples | 64 clay, 96–128 neutral and close-ups, 192–256 dark 9:16 |
| Bounces | 10 total, 4 glossy; caustics off; filter glossy 0.4 |
| View | AgX, look Punchy; exposure −0.55 (neutral studio), +0.3 (dark set) |
| Textures | 4× capture (5792 × 4344), sRGB, cubic interpolation, no sharpening |

## Lights (identical in every neutral render; in the dark set they are linked to the instrument)

| Light | Placement (from the face centre) | Size | Power | Purpose |
|---|---|---|---|---|
| Key softbox | left, slightly forward, 0.88 m up | 1.15 × 0.85 m | 26 W (18 W dark) | form, gradients, knob shadows |
| Fill | front right, low | 1.6 × 1.2 m | 4.5 W | opens the shadows |
| Rim strip | behind-left, 0.16 m up, spread 60° | 1.6 × 0.10 m | 30 W | timber edges, silhouette |
| Fascia kicker | in front, at the level of the timber | 1.4 × 0.06 m | 7 W | a highlight along the front fascia |
| Display card | off camera B's mirror direction, glossy only | 0.7 × 0.12 m | 0.25 W | one soft band on the glass |
| Black flag | camera B's mirror direction, glossy only | 1.8 × 1.2 m | — | keeps the glass and glaze from reflecting the bright studio |
| Floor overhead / rake (dark set only) | as in pass 1, linked to the floor | | | the floor and the instrument's shadow on it |

No ambient occlusion pass is used: all shadows are cast by real geometry.
