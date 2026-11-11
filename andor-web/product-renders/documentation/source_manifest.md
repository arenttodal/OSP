# Source manifest

## The instrument (genuine)

| Source | What it gives | Where |
|---|---|---|
| `web-01-hero-3L@3x.png` | The real ANDOR OSP editor, three layers, 4344 × 3258 (3× of 1448 × 1086), captured headlessly by the hidden test `[web-capture]` at OSP commit `c49d77f` | `assets/source/captures/` (regenerate with `tools/capture.sh`) |
| `web-08-hero-3L-bounds.json` | Every component's bounds in the same state (knob centres, displays) | same |
| Luminance profiles of the capture | Frame geometry: tray, raised panel `x 34.3–1412.3, y 19–1071.5` (r 7 px), teak cheeks `x 3–25 / 1423–1445, y 13–1076` | `blender/textures/geometry.json` (written by `prep_textures.py`) |

Every interface pixel in a render is the capture. Nothing is redrawn.

| Part | How it is made | Source |
|---|---|---|
| Face (tray + panel top) | The capture, projected down the face normal | capture |
| Raised ceramic panel | A 1.2 mm rounded slab at the measured bounds; top = capture, edge = ceramic | measured |
| 25 knob caps (6 macro, 15 layer, 4 envelope) | Revolved caps at the measured centres and radii (47 / 28 / 19 px); tops = capture (so pointers, values and arcs are exactly the editor's); macro skirts knurled (48 ridges, as drawn) | bounds JSON + measured radii |
| Knob shadows | The editor's own drawn shadows, kept in the face texture. The caps cast no second shadow | capture |
| Teak cheeks | Full-depth end cheeks at the drawn width and length; wood = the editor's own teak pixels (`teak_strip.png`), grain along the length on every face | capture |
| Displays | Same pixels; a mask from the display bounds gives them glass (clear coat) and their own glow | bounds JSON |

## Approximations (documented)

- **Scale** 400 × 300 × 24 mm is a visualisation scale, not a hardware dimension.
- **Cheeks** run flush to the outer edge; the editor draws a 3 px (0.8 mm) strip of tray outside them.
- **Body sides, underside, knob sides** are not in any screenshot: plain ceramic in colours sampled from
  the capture's frame and knob rims.
- **Buttons, keys, wheels, sliders** stay flat (part of the face texture). Only knobs are raised.
- **Waveforms** in the capture are deterministic stand-in test signals, as on the site. Re-capture with
  approved recordings (`OSP_WEB_SOURCES`) before any public use.
- The **teak** is colour-matched (+18 % saturation, −28 % value) so that through AgX it renders close to
  the capture's teak; see `materials.md`.

## Reference (not stored in the repository)

A Limo Series "Il Granello" Instagram Story ad supplied by the owner, cropped to remove the Instagram
interface for study. It is someone else's advertisement, so it is kept out of the repo. Only its
qualities were taken, never its controls, text, logo, layout or headline placement.

| Quality | In the reference | In the ANDOR render |
|---|---|---|
| Product bounding box | slab ≈ 1.6 : 1, bleeds off the left edge, ≈ 49 % of frame height | OSP 4 : 3, whole, ≈ 86 % of frame width, ≈ 37 % of height |
| Camera | three-quarter, face fairly frontal, receding to the right | 75 mm, 27° off the face normal, turned 24°; product turned −9° in its plane |
| Housing thickness | visible along the bottom and right edges | 24 mm walls, visible along the front and right (teak) |
| Shadow | very soft, barely visible | soft shadow on the floor 28 cm below, lower right |
| Materials | matte warm ivory, glossy dark screen, matte dark knobs | satin ceramic (0.5), coated glass displays, ceramic knobs (0.34) |
| Background | dark warm-brown diagonal bands, strongly blurred | dark plank floor (peat, walnut, brown, bronze inlays) crossing the product's tilt, f/9 |
| Hierarchy | logo → title → tagline → product → CTA | wordmark → two-line headline → product → space for a CTA |
| Text-safe regions | top 8–30 %, bottom 82–90 % | top 13–33 %, CTA at 78–85 %; Stories UI zones (top 13 %, bottom 18 %) kept clear |
