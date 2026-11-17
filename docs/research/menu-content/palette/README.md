# CORE macro palette study (Gate 2), for approval

Nothing in this study changes the shipped palette. The captures come from the hidden test
`osp_plugin_tests "[palette-shots]"` (`OSP_PALETTE_SHOTS=<folder>`). Each is shown at native
size (scale 1) and at @2x.

## What is there today

| Position | Macro | Token (`design::colour::macro`) | Bead sprite | Effect pedal enamel (`FxIdentity`) |
|---|---|---|---|---|
| 1 | LIFE | rust `#BE7552` | `led-life-*` | — |
| 2 | DRIVE | ochre `#B5843A` | `led-drive-*` | DRIVE olive `#68744C` |
| 3 | FILTER | moss `#7B8D69` | `led-character-*` | — (the voice FILTER rack: `#5F7462`) |
| 4 | MOVEMENT | mineral blue `#73899C` | `led-motion-*` | MOVEMENT terracotta `#A85E42` |
| 5 | ECHO | verdigris `#5C9490` | `led-echo-*` | ECHO slate `#597789` |
| 6 | SPACE | mauve `#96839D` | `led-space-*` | SPACE teal `#4C7771` |

**Findings:**
1. Macros 3 and 4 are two muted greys of nearly equal lightness. That is the reported problem.
2. **The USER macros had no palette of their own:** `UserMacroController::colour` returned
   `colour::macro`. A CORE change would have recoloured the USER bank.
   - This commit freezes it as `colour::userMacro` (Design.h), with the same values. It looks
     identical today (`4-user-bank`).
3. **The beads are rendered sprites**
   (`tools/blender-assets/skin/pieces.py`, `MACRO_COLOURS` → `ceramic-skin.zip`).
   - A new palette needs a Blender re-render of the 12 `led-*` pieces. This container has no
     Blender.
   - In captures 2 and 3 the beads are previews only: the rendered lens recoloured in HSV, with
     its shading and highlight kept.
4. **The effect pedals and racks do not share the macro identities.**
   - They use their own approved V5 enamel seeds, and none matches: DRIVE is ochre against
     olive, MOVEMENT blue against terracotta, ECHO verdigris against slate, SPACE mauve against
     teal.
   - Making them agree changes either the approved pedals or the macros. That is a design
     decision, not a token fix.

## Candidates

| Capture | LIFE | DRIVE | FILTER | MOVEMENT | ECHO | SPACE |
|---|---|---|---|---|---|---|
| `1-core-current` | `#BE7552` | `#B5843A` | `#7B8D69` | `#73899C` | `#5C9490` | `#96839D` |
| `2-core-filter-movement-refined` | — | — | aged teal `#518B88` | antique brass `#B09A61` | — | — |
| `3-core-harmonised` | dried sage `#879674` | burnished copper `#BB784E` | aged teal `#518B88` | antique brass `#B09A61` | warm clay `#94776B` | slate blue `#758899` |

Comparisons:
- `4-user-bank`: unchanged, its own palette now.
- `5-pedals@2x`: the approved V5 pedals, unchanged.

**Recommendation:** capture 3, the harmonised palette.
- Capture 2 separates 3 from 4, but makes FILTER's aged teal (`#518B88`) almost the same colour
  as ECHO's verdigris (`#5C9490`): the 3/4 problem moves to 3/5.
- Capture 3 keeps six distinct, muted, earthy hues at a similar lightness.

## On approval

1. Set the six tokens in `design::colour::macro`.
2. Re-render the `led-*` pieces with the same colours (`pieces.py` `MACRO_COLOURS`, then
   `generate.py` and `pack.py`) on a machine with Blender. Replace `ceramic-skin.zip`.
3. Decide about the pedals:
   - **(a)** keep their V5 enamels (they are devices, not macros);
   - **(b)** derive each shared effect's enamel from its macro token, through a shared token
     (DRIVE, MOVEMENT, ECHO, SPACE).
4. Then:
   - the visual references (`[design-capture]`, `osp_visual_reference`) update with the
     approved change;
   - the USER palette stays as it is.
