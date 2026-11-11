# Materials

| Material | Used on | Build |
|---|---|---|
| **OSP Face (capture)** | tray top, panel top, knob tops | Principled. Base = the capture (sRGB, cubic), projected from the product root's object space. Roughness 0.50 ceramic → 0.22 on displays. **Displays are light under glass, not paint:** there the base colour drops to near-black, the base specular to 0, and the pixels are emitted (× 1.7) under a clear coat (weight 0.7, roughness 0.035). This keeps the screens as deep as the editor draws them (a lit dark albedo was lifted to slate grey by AgX). Ceramic emits a whisper (× 0.03) so labels stay legible in shade; fine noise bump on ceramic only |
| **Ceramic Ivory** | panel edge | Principled, sRGB 232 224 211, roughness 0.40 ± 0.05 (noise), micro bump |
| **Ceramic Tray** | body walls, underside | sRGB 214 203 189 (sampled from the capture's frame), roughness 0.46 |
| **Ceramic Knob** | knob skirts | sRGB 236 230 219 (knob rims), roughness 0.34 |
| **ANDOR Teak (capture)** | cheeks, all faces | the editor's teak pixels; u from x (top) or z (sides), v along the length, mirrored; saturation ×1.18, value ×0.72 so it renders like the capture through AgX; roughness 0.55–0.72 from the grain; grain bump |
| **Backdrop 0–7** | floor planks | dark dielectrics (peat charcoal, deep warm brown, muted walnut), roughness 0.30–0.52, fine groove bump along the plank; two 6 mm dark-bronze metallic inlays |

No metal is used on the instrument: nothing in OSP's design is metal. No screws, vents, ports or
cables were added.

## Colour check (render vs capture)

Flat areas sampled in the hero render against the same areas of the capture:

| Area | Capture (sRGB) | Render | Note |
|---|---|---|---|
| Display background (layer B) | 32 40 43 | 34 42 43 | matched after making the displays emissive |
| Ceramic panel, lit side | 233 225 211 | ≈ 221 216 210 | intentional: lit by the key with falloff, AgX Punchy |
| Teak cheek top | 189 136 101 | warm brown, slightly darker on the shaded faces | colour-matched in the shader |
