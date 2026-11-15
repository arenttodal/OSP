# Knob height test: measured geometry (pass 3.5, checkpoint 1)

Measured from the built meshes by `andor_scene.py --report` (one JSON per variant in this folder).
Design mm = the official generator's units (D = 28 mm); scene mm = after scaling to the editor's knob radius (46.48 px at 0.276 mm/px).

| Variant | H/D | Height (design) | Cap D | Base ring | Top radius | Bevel | Crown | Flute depth | Flutes counted | Height (scene) | Clearance to LED / neighbour (px) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| E | 0.25 | 7.0 mm | 28.0 | 28.9 | 13.02 | 0.9 | 0.4 | 0.26 | 48 | 6.22 mm | 14.02 / 53.04 |
| F | 0.27 | 7.56 mm | 28.0 | 28.9 | 13.02 | 1.5 | 0.55 | 0.252 | 48 | 6.717 mm | 14.02 / 53.04 |
| G | 0.3 | 8.4 mm | 28.0 | 28.9 | 13.02 | 1.5 | 0.55 | 0.252 | 48 | 7.464 mm | 14.02 / 53.04 |
| H | 0.33 | 9.24 mm | 28.0 | 28.9 | 13.02 | 1.5 | 0.55 | 0.252 | 48 | 8.21 mm | 14.02 / 53.04 |
| D | 0.3929 | 11.0 mm | 28.0 | 28.9 | 13.02 | 1.5 | 0.55 | 0.42 | 48 | 9.774 mm | 14.02 / 53.04 |

Pointer angles (rad, LIFE … SPACE, identical in every variant): 6.8487, 3.927, 7.4142, 5.2465, 6.0947, 4.7752

Automated checks (every variant):

- E: six_macros ✓, macro_diameter_identical ✓, macro_height_identical ✓, all_knobs_seated_on_their_surface ✓, flutes_48 ✓, rails_same_height ✓, macro_clearances_positive ✓
- F: six_macros ✓, macro_diameter_identical ✓, macro_height_identical ✓, all_knobs_seated_on_their_surface ✓, flutes_48 ✓, rails_same_height ✓, macro_clearances_positive ✓
- G: six_macros ✓, macro_diameter_identical ✓, macro_height_identical ✓, all_knobs_seated_on_their_surface ✓, flutes_48 ✓, rails_same_height ✓, macro_clearances_positive ✓
- H: six_macros ✓, macro_diameter_identical ✓, macro_height_identical ✓, all_knobs_seated_on_their_surface ✓, flutes_48 ✓, rails_same_height ✓, macro_clearances_positive ✓
- D: six_macros ✓, macro_diameter_identical ✓, macro_height_identical ✓, all_knobs_seated_on_their_surface ✓, flutes_48 ✓, rails_same_height ✓, macro_clearances_positive ✓

E keeps round 1's own profile (0.9 mm bevel, 0.4 mm crown, base ring 1.1 + 0.3 mm). F, G, H are the official profile with only the total height changed and 0.252 mm flutes; D is the official knob unchanged (0.42 mm flutes).
