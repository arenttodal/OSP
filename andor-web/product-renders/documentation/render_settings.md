# Render settings

| Setting | Value | Why |
|---|---|---|
| Engine | Cycles, CPU (this environment has no GPU; use GPU where available) | physically based light transport |
| Samples | 256 final (adaptive, threshold 0.015), 24–48 drafts | the face texture is minified ~4.6×; samples also anti-alias the interface text |
| Denoiser | OpenImageDenoise | |
| Bounces | 8 total, 4 glossy; caustics off; filter glossy 0.5 | |
| View transform | **AgX, look "Punchy", exposure +0.3** | compared with AgX Base/Medium High, Standard (clips the ceramic) and Filmic (lifts the displays): Punchy kept the interface's contrast best |
| Colour space | capture in sRGB; display mask Non-Color | |
| Output | PNG 16-bit; RGBA for the transparent variant | |
| Pixel filter | 1.2 px | slightly crisper interface type |

## Lights (all area lights)

| Light | Position (relative to the face centre) | Size | Power | Linked to |
|---|---|---|---|---|
| Key softbox | upper left, 1.05 m above | 0.8 × 0.55 m | 11 W | instrument |
| Fill | front right, low | 1.6 × 1.2 m | 5.5 W | instrument |
| Rim | behind, 0.26 m above, spread 70° | 1.4 × 0.12 m | 40 W | instrument |
| Floor overhead | upper left, 1.5 m above the floor | 1.0 × 0.9 m | 34 W | floor (the instrument casts its shadow) |
| Floor rake | far left, grazing | 2.8 × 0.18 m | 26 W | floor |
| World | warm grey 0.02, strength 0.12 | | | all |

Light linking keeps the instrument's exposure and the floor's darkness independent, as flags and
cutters would in a studio. The key and the floor light come from the same side, so the shadow
direction agrees with the light on the face.
