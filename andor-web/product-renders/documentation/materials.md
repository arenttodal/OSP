# Materials

| Material | Used on | Build |
|---|---|---|
| **OSP Face (capture)** | tray top, panel top, knob tops | Principled. Base = the capture (sRGB, cubic), projected from the product root's object space. Roughness 0.50 ceramic → 0.22 on displays; clear coat 1.0 (roughness 0.035) on displays only; emission = capture × 0.03 on ceramic (keeps labels legible in shade) and × 1.15 on displays (screens glow); fine noise bump on ceramic only |
| **Ceramic Ivory** | panel edge | Principled, sRGB 232 224 211, roughness 0.40 ± 0.05 (noise), micro bump |
| **Ceramic Tray** | body walls, underside | sRGB 214 203 189 (sampled from the capture's frame), roughness 0.46 |
| **Ceramic Knob** | knob skirts | sRGB 236 230 219 (knob rims), roughness 0.34 |
| **ANDOR Teak (capture)** | cheeks, all faces | the editor's teak pixels; u from x (top) or z (sides), v along the length, mirrored; saturation ×1.18, value ×0.72 so it renders like the capture through AgX; roughness 0.55–0.72 from the grain; grain bump |
| **Backdrop 0–7** | floor planks | dark dielectrics (peat charcoal, deep warm brown, muted walnut), roughness 0.30–0.52, fine groove bump along the plank; two 6 mm dark-bronze metallic inlays |

No metal is used on the instrument: nothing in OSP's design is metal. No screws, vents, ports or
cables were added.

## Colour check (render vs capture)

Run after each final render; see `render_settings.md` for the values measured on the hero.
