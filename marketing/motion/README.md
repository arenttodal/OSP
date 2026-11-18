# ANDOR Motion Library

Reusable product-cinematography clips from the approved ANDOR OSP Blender master, made under the
brief in [docs/marketing/blender-motion-library.md](../../docs/marketing/blender-motion-library.md).
The creative strategy behind them is in the [launch playbook](../../docs/marketing/andor-launch-playbook.md).

```
marketing/motion/
  configs/     one versioned config per shot (camera keys, timing, draft and still settings, reframes)
  scripts/     render_shot.py (bpy): one shot from its config · review_gate.py: a gate's review set
  manifests/   one manifest per shot (Gate D4's fields), written by render_shot.py
  review/      manageable review assets per gate (contact sheet, stills, preview MP4s) - committed
  media/       frames, draft MP4s, full-size stills - NOT in Git (.gitignore); paths in the manifests
```

## How a shot is made

- **The approved master, unchanged.** `render_shot.py` builds each shot's scene with
  `andor-web/product-renders/blender/scripts/andor_scene.py`, from the shot's campaign setup and
  base shot (`andor-web/product-renders/blender/config/campaign_setups.json`).
  - Geometry, materials, lights, floor, world and colour management (AgX, AgX - Punchy) are
    exactly those of the approved C3 stills.
  - The editor face comes from the current editor capture (`prep_textures.py` into
    `textures_current/`).
- **Only the camera moves.** Its parameters are the campaign shots' own: yaw and pitch about the
  shot's target, distance, lens, target, f-stop and shift.
  - A config gives keys over a 0..1 timeline, each overriding some parameters. `distScale`
    multiplies the base shot's distance.
  - Values are interpolated on an eased timeline (smootherstep: no velocity or acceleration at
    either end).
  - Focus follows the target in every frame.
  - The product never moves, so its floor shadow is the scene's own, path-traced per frame
    (no sliding shadow assets).
- **Resumable.** Frames already on disk are kept. `--frames a:b` renders part of a range.
- **Outputs.**
  - Frames, the draft MP4 (H.264, 30 fps) and the opening, middle and closing stills.
  - A high-quality still at the config's moment.
  - Reframe tests (their own base shot and composition, not crops) as stills.
  - The camera curve and the manifest.

```sh
P=<python with the bpy module, e.g. a venv with bpy 4.2>
cd andor-web && $P product-renders/blender/scripts/prep_textures.py --out product-renders/blender/textures_current && cd ..
$P marketing/motion/scripts/render_shot.py --config marketing/motion/configs/D01_gallery_reveal.json
$P marketing/motion/scripts/review_gate.py --gate D1 --shots D01 D02 D03
```

## Gate D1 (drafts for creative review)

| Shot | Config | Base (approved) | Move | Length | Draft |
|---|---|---|---|---|---|
| D01 Gallery reveal | `D01_gallery_reveal.json` | ANDOR_GALLERY A1-R1 W (85 mm, f/11) | push in with a shallow arc: yaw −37° to −30°, pitch 54.5° to 52°, distance ×1.08 to ×1. It ends on the approved hero, which is also the end poster. Portrait reframe test on A1M-R1 | 7 s | 1280 × 720 |
| D02 Dark orbit | `D02_dark_orbit.json` | ANDOR_FLOATING B1-R4 (85 mm, f/9) | 12° orbit: yaw −42° to −30°, pitch 59° to 57°. The middle frame is the approved B1-R4. Vertical reference on B2 +15 % (B2_115_R4) | 7 s | 960 × 1200 (4:5) |
| D03 Macro glide | `D03_macro_glide.json` | ANDOR_MATERIAL M01 (100 mm, 0.2 m) | lateral track across three macro knobs (editor x 400 to 640) at f/11, focus on the knobs' plane | 6 s | 1280 × 720 |

The review set is in `review/D1/`. Nothing here is final. Full-resolution masters, D04–D08 and
any publishing wait for creative approval (Gate D2/D3).
