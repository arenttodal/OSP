# 05 — Assets

The rendered assets are the instrument's material. They are embedded in every product by
`AndorUiData` (`shared/andor-ui/AndorUi.cmake`), the same binary data target for all.
Licensing and redistribution: `../assets/manifest.json`.

| Asset | Path | Made by | Used by |
|---|---|---|---|
| Inter: Light, Regular, Medium, SemiBold, Bold, ExtraBold (TTF) | `shared/andor-ui/Resources/fonts/` | Upstream, SIL OFL 1.1 (`Inter-OFL.txt`) | `fonts::make` |
| Outfit: ExtraBold, ExtraLight (TTF) | same | Upstream, SIL OFL 1.1 (`Outfit-OFL.txt`) | the identity |
| Ivory macro knob, 176 and 352 px frames (zip of PNG frames + manifest) | `shared/andor-ui/Resources/knobs/macro-ivory/` | `tools/blender-assets` (Blender: `config/macro-knob.json`, `generate.py`, `postprocess.py`) | `SpriteKnobRenderer` (macros) |
| Ceramic skin: 80 pieces (housing with teak rails, cards 451/683/1379, panels, raised faces, keys in four states, badges, bezels, fader, lenses, keybed), several levels each | `shared/andor-ui/Resources/skin/ceramic-skin.zip` (`manifest.json` inside) | `tools/blender-assets/skin` (Blender: `pieces.py`, `render.py`, `pack.py`) | `CeramicSkin` (`skin::Pieces`, nine-slice at their rendered places) |

## Rules

- **Never redraw an asset with JUCE primitives** (gradients for the teak, a drawn knob for
  the ivory cap, and so on). Without the pack the code falls back to the classic drawing: a
  fallback, never a design.
- **Never generate new artwork** (no AI images). New rendered parts come from the Blender
  generators in `tools/blender-assets`, with the same materials, lighting and camera.
- **Regenerating:** follow `tools/blender-assets/README.md`. The packers write to
  `shared/andor-ui/Resources/`. Every product picks the change up on its next build, and the
  visual regression shows exactly what changed.
- **Fonts:** keep the OFL license files next to the fonts in any distribution. Do not
  rename the fonts.
