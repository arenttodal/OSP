# ANDOR/OSP Design Lab

This is an exploration workspace for premium GUI materials. Its scope:

- Blender renders the surfaces.
- They are composited under the plugin's own drawing.
- The results are presented for a decision.

It has **no production integration.** The plugin's code, assets, layout, parameters and
presets are untouched. Nothing in this folder is compiled into the plugin.

Start with:

- `00-current-ui-audit.md` (the baseline);
- `review/index.html` (the gallery);
- `review/comparison-report.md` and `review/recommendations.md`.

## Layout

```
00-current-ui-audit.md          baseline audit (dimensions, tokens, states, inconsistencies)
references/
  current-gui/export{1x,1.5x,2x,4x}/   the production editor, each component's own paint as a layer
  approved-knob/README.md        where the approved knob lives (not duplicated)
  uimother/README.md             Eclyde kit: not available here; nothing redistributed
blender/
  generate.py                    entry point: calibrate | swatches | scene <direction:..|family:..>
  scenes.py                      the chassis builder (every surface as real geometry, GUI space)
  studies.py                     the family studies (variant x state x crop), pure data
  lab/geometry.py  lab/forms.py  rounded-rect slabs with pockets (bevels, lips, grooves, floors)
  lab/materials.py               PBR materials from configs/materials.json
  lab/studio.py                  light rig (the knob's rig as suns + overhead softbox + scrim), camera, colour
  configs/materials.json         M1-M6 (+ knob ivory, graphite key, dark slot)
  configs/controls.json          families A (buttons), B (tabs), C/T (fader caps/tracks), D (macro panels),
                                 E (enclosures), geometry of the production layout, colour roles
  configs/directions.json        the six full-GUI directions (one choice per family)
  configs/calibration.json       exposure (iterative, M1 plate)
scripts/
  DesignLabExport.inc            hidden Catch2 test that exports the editor as layers
  calibrate_colours.py           matches Direction 01's surfaces to the production GUI (< 1 %)
  composite_gui.py               lays the plugin's drawing over a rendered chassis (directions, studies)
  render_all.py                  every direction and study at final quality, then composites
  build_review.py                review package: full GUIs, actual-size, sheets, close-ups, contact sheet
  swatch_sheet.py                material swatch sheet
renders/                         Blender output (chassis, studies, material swatches)
mockups/                         composites (full-gui/, components/)
review/                          the presentation package and gallery
```

## Reproducing

The `bpy` 4.2 module runs as plain Python (`pip install bpy==4.2.0` in a Python 3.11 venv).
Blender's own `blender -b -P generate.py -- ...` works too.

```sh
# 1. The production GUI as layers (once per GUI change). Append the include, build, export, revert:
echo '#include "../../design-lab/scripts/DesignLabExport.inc"' >> tests/plugin/PluginProcessorTests.cpp
cmake --build build-plugin --target osp_plugin_tests
for s in 1 1.5 2 4; do OSP_DESIGNLAB_SCALE=$s OSP_DESIGNLAB_DIR=$PWD/design-lab/references/current-gui/export${s}x \
  ./build-plugin/apps/plugin/osp_plugin_tests "[design-lab]"; done      # (rename full@2x of 1.5 -> full@1.5x)
git checkout tests/plugin/PluginProcessorTests.cpp

# 2. Calibrate, render, composite, build the review (from design-lab/)
cd design-lab/blender && $BPY generate.py calibrate && cd ..
python3 scripts/calibrate_colours.py $BPY          # Direction 01 surfaces = production colours
python3 scripts/render_all.py $BPY                 # ~2 h on 4 CPU cores
python3 scripts/build_review.py
```

Every render is deterministic:

- fixed Cycles seed;
- no adaptive sampling;
- configuration only in `configs/`.

## Method (why the mockups are honest)

1. **One geometry for everything.** The housing, cards, display bezels, macro panel, buttons,
   tabs, fader and LEDs are slabs and pockets: real bevels, lips, grooves and floors.
   They are built from the production layout (`layers.json` bounds, `Design.h` radii) at the
   approved knob's scale (1 design px = 0.31 mm).
2. **One light.** The knob's `studio-soft` rig, as sun lamps with the same directions and
   angular sizes, plus an overhead softbox fixed in GUI space. Every element, in every
   direction and study, is lit identically.
   - A reflection-only scrim lets satin and metal show their finish when seen straight down.
   - Lights are neutral; the warmth is in the materials.
3. **One exposure and calibrated colour.** The exposure comes from an M1 plate. The ivory base
   colours are then scaled so that Direction 01's housing, cards, panel and backdrop match the
   production GUI (largest channel error 0.5 %).
4. **The plugin draws everything else.** Text, icons, waveforms, knobs, halos, the keyboard
   and the envelope editor come from the plugin's own renderer (the layer export) at each
   scale. The compositor never redraws them, with two exceptions, which are flat by design:
   - B1's underline (crisper);
   - a fader's software value fill.
   On surface layers only the ink (strokes) transfers:
   - as a ratio in linear light, so a dark word stays dark on a new ivory;
   - the old painted edges and shadows are excluded (bands along every outline);
   - on graphite, ink is recoloured light.

### Colour management (renders)

| Setting | Value |
|---|---|
| Renderer | Cycles CPU, OpenImageDenoise, filter width 1.5 px |
| View transform | Standard |
| Look | None |
| Exposure | from `configs/calibration.json` |
| Gamma | 1.0 |
| Display | sRGB |
| Output | 8-bit RGBA PNG, straight alpha, film transparent (the chassis is opaque) |
| Masters | 2× design for the full GUIs, 4× for small-control studies |
| 1× and 1.5× views | area-resampled from the 2× masters |

## Hard rules followed

- No production asset, control, layout, parameter, DSP or preset was changed.
- The plugin was not rebuilt for this; only the test binary, to export layers.
- The approved knob is used, never re-rendered or altered.
- No licensed kit asset is used or redistributed.
- No performance figure is claimed for any asset; none is integrated.
