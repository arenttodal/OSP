# Rendered controls (Blender)

Procedural, headless Blender generator for OSP's physical-looking controls. Phase 1 makes one
control: the macro knob, `macro-ivory`. Everything is regenerated from the JSON configs and
presets here. Nothing is modelled by hand, and no Blender file is kept.

```
tools/blender-assets/
  generate.py          renders (needs Blender's Python: `blender -b -P` or the `bpy` module)
  postprocess.py       sheets, runtime packs, atlas, manifest (Python 3 + Pillow only)
  osp_assets/          the library: config + presets, geometry, materials, studio
  config/              one JSON per control (config/future/: planned controls)
  materials/           finishes (ivory: soft, brushed, satin) and indicator inks
  lighting/            the fixed studio rig(s)
  scenes/              orthographic camera presets
  output/              renders and previews (git-ignored; regenerate them)
```

Runtime resources go to `apps/plugin/Resources/knobs/<name>/<name>-<size>.zip`. They are
embedded by `juce_add_binary_data` and read by `apps/plugin/Source/SpriteKnobRenderer`.

## Running it

Blender 4.2. Either install Blender and pass the script, or use the Python module
(`pip install bpy==4.2.0` needs Python 3.11):

```sh
cd tools/blender-assets
python generate.py --config config/macro-knob.json --stage calibrate    # exposure for the target ivory
python generate.py --config config/macro-knob.json --stage variants     # the three finishes
python generate.py --config config/macro-knob.json --stage indicators   # engraved vs printed
python generate.py --config config/macro-knob.json --stage cameras      # camera elevations
python generate.py --config config/macro-knob.json --stage frames       # the 129-frame turn
# or: blender -b --factory-startup -P generate.py -- --config config/macro-knob.json --stage frames

python3 postprocess.py --config config/macro-knob.json --stage variants  # (and indicators, cameras)
python3 postprocess.py --config config/macro-knob.json --stage frames    # packs, atlas, manifest, contact sheet
```

Useful options:
- `--set key.path=value` overrides any config value. The value is JSON, e.g.
  `--set indicator.width=1.4 --set frames.count=257`.
- `--frames N` sets the frame count; `--only 0,64,128` renders just those frames.
- `--resolution`, `--samples` and `--out` make quick drafts.

Each frame takes about 40 s on 4 CPU cores (512 px, 96 samples, denoised), so the full turn
takes about 1.5 h. Frames already on disk are skipped unless `--force` is given.

To make the comparison sheets include the current vector knob, set
`OSP_CURRENT_KNOB_SHOT` to a canonical 1448 × 1086 screenshot.

## What it makes

**Geometry** (`osp_assets/geometry.py`):
- A profile of revolution: a narrow base ring, a small groove, a tapered cap sidewall, a soft
  bevel and a gently domed top.
- Every corner is filleted.
- Flutes are a radial displacement of the sidewall. They fade in above the groove and out
  across the bevel, so the top outline is only gently scalloped.
- All sizes are in mm, set in `geometry`.

**Materials** (`materials/ivory.json`):
- A Principled BSDF finish with a fine procedural grain (powder or circular brushed) and a
  slight roughness variation.
- The indicator is in the same shader, as an object-space mask along +Y, so its edges are
  exact at any size.
- `engraved` cuts a shallow rounded groove (bump) with a matte charcoal inlay. `printed` is
  flat ink.

**Studio** (`lighting/`, `scenes/`):
- A fixed rig of a large soft key at the upper left, a fill and a faint edge light, plus soft
  ambient.
- A shadow-catching panel keeps only the contact shadow, in the alpha.
- The orthographic camera looks straight down for the GUI.
- The rig never moves: only the knob turns, so its light behaves like a real knob's.
- `calibrate` finds the exposure that puts the top face on the config's target colour, so
  the knob sits in the palette, not near it.

**Frames**:
- Frame 0 is the minimum and the last frame the maximum.
- Angles are JUCE's rotary convention, copied from `OspLookAndFeel::rotaryStart` and
  `rotaryEnd` (1.25 π .. 2.75 π, clockwise from 12 o'clock).
- `[plugin][sprite-knob]` reads the indicator's angle out of every frame's pixels and checks
  it against this mapping.

**Determinism**: there is a fixed seed and no adaptive sampling, and the same settings give
the same pixels on the same machine. OIDN denoising can differ in the last bit between CPUs.

## Runtime format

One zip per size: `manifest.json` plus `frames/000.png ...`.
- PNGs are stored, not deflated, since they are already compressed.
- Timestamps and order are fixed, so the same frames give the same bytes.
- The atlas (`output/<name>/atlas-<size>.png`, row-major, frame 0 at the top left) and
  `output/<name>/manifest.json` are for people and other tools. The plugin does not read
  them.

Manifest (schemaVersion 1):

| Field | Meaning |
|---|---|
| `frameCount`, `frameSize` | frames and their side in pixels |
| `pivot` | the rotation centre, as a fraction of the frame (0.5, 0.5) |
| `bodyRadius` | the knob's outer radius as a fraction of the frame side: the plugin sizes the frame so this equals the drawn knob's body radius |
| `angleStart`, `angleEnd` | the rotary angles of the first and last frame |
| `frames` | `frames/%03d.png` |
| `source` | config hash, Blender version, material, exposure, master resolution, samples |

The plugin side (`SpriteKnobRenderer`):
- It decodes a frame only when that frame is first shown.
- It resamples the frame once, by area averaging, to exactly the physical pixels it covers at
  the current window and display scale, then draws it 1:1.
- Every editor in the process shares one copy.
- Only the cap is rendered. Ticks, the value arc and the modulation halos stay vector-drawn
  around it.

## Adding a control

1. Add `config/<control>.json`. Reuse the material, lighting and camera preset names, and
   change geometry and indicator sizes, the target and the runtime levels. Size the runtime
   levels from the control's drawn size: the frame side is about 1.45 × the body diameter,
   and you need levels at about 1× and 2× the default window size.
2. Render the stages and judge the sheets at real size, not at the large size.
3. `postprocess.py --stage frames` writes the packs. List them in
   `apps/plugin/CMakeLists.txt` (`juce_add_binary_data`).
4. In the plugin, give the slider the property `sprite = "<name>"`. `design::draw::knob` then
   draws the rendered cap.

Planned controls (`config/future/`) describe sizes and what is new for each. Knob-type
controls only need a config. The fader cap and toggle switch need a geometry builder
(`osp_assets/geometry.py`) and, at runtime, a frame per position or state rather than per
angle.

## Not committed

`output/` (masters, atlases, previews) is regenerated, not stored. The comparison sheets
chosen for review are copied to `docs/reports/knob-phase1/`.
