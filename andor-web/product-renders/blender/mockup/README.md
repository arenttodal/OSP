# Mockup studio

**What it does.** Puts real ANDOR OSP editor captures on a display at an angle, in a soft studio, Apple-style, or behind the 3D instrument as hero backdrops. Everything renders on the CPU with the `bpy` module, so it adds no GPU cost.

## Pieces

| File | What it does |
|---|---|
| `screen_mockup.py` | Renders any image on a minimal display (aluminium shell, black glass, emissive screen with a glass coat) in a seamless studio. Presets: `float`, `three`, `low`, `macro` (close and raking onto one point, shallow focus), `curved`, `desk` (on a stand). Tones: `light`, `dark`, `warm`. Options: `--orbs` (frosted spheres), `--zoom`, `--focus u,v` |
| `screen_content/daw.html` + `render_content.cjs` | The screen content: an original, generic DAW session, with the real editor capture floating over it in a window (`layout=window`), or the session alone (`layout=daw`) |
| `make_shots.py` | The shot list: renders the content, then the display. For a close-up it aims the camera at the centre of a part, using the capture's own bounds JSON |
| `screen_content/backdrops.html` + `render_page.cjs` | Hero backdrops drawn from the session: `curved` (a curved session wall), `float` (floating fragments), `wall` (a blurred wall of colour), `soft` (glass orbs), `warm` (the flat editor on a colour field with a long shadow) |
| `composite_hero.py` | Puts the H01 instrument in front of a backdrop, using the cine rig's beauty, matte and clean passes. The floor's shading carries over (shadow ratio = beauty ÷ clean), so the contact shadow survives |

## Use

```bash
# 1. real captures from the release editor (tests/plugin/WebCaptureTests.inc)
OSP_WEB_CAPTURE=/tmp/caps xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[web-capture]"
# 2. all shots (about 4-8 min each at 1920 x 1080, 64 samples, on 4 CPU cores)
python3 make_shots.py /tmp/caps /tmp/shots --python <venv with bpy>/bin/python
# one display shot from any image
<venv>/bin/python screen_mockup.py -- --image content.png --preset macro --focus 0.4,0.55 --tone dark --out shot.png
```

**Captures.** Every open popover is also saved on its own (`<shot>-panel@3x.png`, transparent around it) for clean, uncropped panel images.

**Versions.** Use the frozen release interface (0.9.0-beta.1, `32e0bee`). That commit has no web-capture test; copy `WebCaptureTests.inc` into a worktree of it. The test-only `captureWithoutCaps` switch is missing there, so define it locally; shot 10 is then unused.
