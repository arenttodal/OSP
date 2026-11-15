# Materials (pass 2)

| Material | On | Build |
|---|---|---|
| **OSP Face (capture)** | the tops of the panel, cards, buttons, key bed and black keys | Lit ceramic, **no emission**. Base = the 4× capture (sRGB, cubic), projected from object space. Roughness 0.48, specular 0.22, a 0.05 glaze coat, very fine micro-bump |
| **OSP Display (capture)** | the well floors | A screen is light under glass: near-black base, no base specular, the pixels emitted ×1.7, clear coat 0.45 (roughness 0.03) |
| **Display bezel** | well walls | satin near-black |
| **Ceramic ivory / card / tray** | panel edges, card edges, housing tray | colours sampled from the capture's panel (228 220 206), card (252 246 236) and tray (222 213 198) |
| **Ceramic knob** | knob caps | sRGB 245 241 233, roughness 0.30, coat 0.25 (the glaze) |
| **Knob collar / indicator** | base clearance, pointer inlay | warm dark (52 47 42), charcoal (40 38 36) |
| **ANDOR Teak** | frame | The editor's own teak pixels (both cheeks, stacked to 583 mm so the grain does not repeat on a rail). Long grain runs along each rail's length on its top and long faces, from its own offset; the rail ends show darker, rougher end grain. Saturation ×1.12 and value ×0.74 bring it close to the capture under AgX: the front fascia measures 183 137 104 against the capture's 189 136 101. Roughness 0.48–0.62 follows the grain; oiled, coat 0.08 |
| **Plinth** | underside | dark warm charcoal, satin |

Nothing in OSP is metal, so the instrument has no metal: no screws, vents or ports.

## Lessons from pass 2's diagnostics

- **Screens veiled grey.** The well floor sat below the panel top, so the panel showed through. The cards are
  now 1.2 mm thick, which keeps the floor inside the card.
- **Logo washed out.** This was the rim strip's reflection near the camera's mirror angle. The rim now sits
  behind-left, so it traces the timber without veiling the glaze. Tested by zeroing the glaze specular.
- **Glass reflecting the bright studio.** A black flag is visible only in reflections, as a photographer would
  set one. A weak strip card adds one band of reflection.
