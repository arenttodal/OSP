# Cinematic Direction 2.0: principles, rig and the first creative gate (R01–R03)

**What stays fixed:** the approved model, materials, industrial design and stills.
**What changes:** the cinematography.
**Status:** this document covers the first creative gate only: R01, R02 and R03, three alternatives each.

## 1. Archive and pause

| What | Status |
|---|---|
| **Gate D2 drafts D01–D07** and the G2 / S6 plans in `scripts/motion_preview.py` | Archived. The script and `documentation/gate_d2_motion.md` stay in Git as the record. The frames stay in production storage and are not rendered further |
| **The D02 one-second final test and the S6 studies** (highlight, focus pull, D06b, D07) | Paused |
| **The flicker investigation** | Paused: its process is suspended, not killed. It resumes for the production estimate, because it measures the renderer, not a shot |
| **The new library** | Shot IDs **R01–R08**, under `blender/cine/`. They never reuse D-numbers. Film IDs (FILM-D1…) stay a separate namespace in the Launch Kit |

## 2. What the references teach

**Access:** some reference sites could not be fetched from this environment. The network policy denied territorystudio.com and patrickcasella.com. These principles therefore come from prior study of the films, not a fresh frame-by-frame review. Allow those hosts in the environment's network settings and I will annotate specific frames.

### Composition
- **The object is a landscape before it is a product.** Openings fill or overfill the frame with one surface (a cushion, a dial, a machined edge), and the viewer reads material first and silhouette later.
- **Off-centre is the default; centred is a resolution.** The camera aims ahead of or beside the object while it travels, and only the final frame earns symmetry.
- **Negative space is reserved for type from the start.** The end composition leaves an empty field where the title will sit, so typography never has to fight the product.

### Camera choreography
- **One move per shot, slow and continuous.** A single dolly, crane or arc carries the shot, with no cuts within it.
- **A long settle.** Velocity curves start gently and decay over the last third, and the camera is still moving, barely, when it arrives.
- **Physical moves, not zooms.** Parallax between near and far features is what makes CG read as photographed.
- **No roll unless it means something.** The horizon stays true.

### Lighting
- **Light reveals form; the camera does not need to.** A strip or card passing across a surface shows curvature, grain and edges in sequence. The camera can stay locked.
- **Large soft sources with negative fill.** Broad, gradated highlights, with dark sides held by flags. This is the IVORY GALLERY.
- **Near-black stages with rim and top light.** The object is drawn by its edges. This is the DARK THEATRE.
- **No bloom, flares, particles or glow.** The light lives on the material and never in the air.

### Reflections
- **Reflections are designed objects.** Cards placed out of frame give each surface one clean, readable highlight that travels as the object or the light moves.
- **Displays must not mirror the softbox.** The camera height and the overhead level are set so the screens read as screens.

### Product movement
- **The object moves as one body, about its own centre.** Tilts are small (15–25°) and slow, and shadows and contact are rendered every frame. A moving product never keeps a static shadow footprint.
- **Motion has weight.** It accelerates gently, never overshoots, and settles.

### Typography
- **Type is part of the space.** A monumental wordmark sits on the far wall and the product occludes it. The type resolves only when the product settles.
- **Type is composited, never baked.** Clean masters plus mattes keep every frame re-titlable.

### Rhythm
- **Slower than a launch spot.** Hero shots run 8–12 s each, with no rapid editing.
- **The cut lands on stillness, or on a musical event.** Masters carry no music and no transitions.

## 3. The rig (`blender/cine/cine_rig.py`)

**How shots are defined:** JSON specs (`blender/cine/shots/*.json`, written by `make_shots.py`) drive the approved master built by `scripts/andor_scene.py`.

**Channels.** Each channel is keyed in seconds with its own curve:

| Channel | Controls |
|---|---|
| `camera.position` | Camera translation (product frame, metres) |
| `camera.target` | Animated target, independent of position. The camera does not have to look at the centre |
| `camera.focus` | Animated focus point, independent of the target |
| `lens` | Focal length (mm) |
| `fstop` | Aperture |
| `roll` | Explicit roll, for intent only. Look-at uses world-up, so there is no accidental roll |
| `shift` | Lens shift |
| `product.tilt` / `turn` | Degrees, about a centred pivot |
| `product.translate` | Metres |
| `lights.<name>.energy` | Energy multiplier |
| `lights.<name>.orbit` | Orbit (degrees) |
| `lights.<name>.translate` | Metres |
| `cards[]` | Reflection cards: emissive planes seen only in reflections (optionally in diffuse), with animated position, aim and strength |
| `exposure`, `world`, `emission` | Exposure offset (stops). Multipliers for the world light and for the instrument's self-lit displays, so a shot can emerge from darkness without glowing screens leading it |

**Easing:** `linear`, `smooth`, `in`, `out`, `in_out`, `settle`, `hold`, or any cubic bezier.
- The house curve is `[0.45, 0, 0.25, 1]`: a slow start, still moving at three-quarters, and a quiet arrival.
- Curves are evaluated per frame (not as Blender f-curves), so each shot owns its velocity curve exactly.

**Passes** (same animation, deterministic seed):

| Pass | What it is |
|---|---|
| `beauty` | The clean master, no typography |
| `matte` | Product alpha, environment held out |
| `clean` | The environment without the product: the background pass. Shadow pass = beauty / clean outside the matte |

**Handles:** at least 1 s; the handles hold the end poses.

**Frame rate:** 24 fps.

**Lenses:** 50–85 mm for heroes, 85–105 mm for details.

**Fixed during calibration:** the product frame is now measured from the instrument only. Before the fix, the backdrop's floor planks were counted, which put the DARK THEATRE centre 8 cm low.

**Typography:** `launch-kit/typography/compose_wordmark.py` composites the wordmark behind the product through the matte.
- The wordmark is sized and placed from the closing frame, so it ends fully readable.
- The meta file records the fraction of the wordmark occluded on every frame.
- Fonts: Inter Tight 760 (ANDOR) and EB Garamond (the headline "One sound. An entire instrument."). Both are OFL and live in `launch-kit/typography/fonts`.

## 4. Shots

Per-shot narratives and camera, object and light parameters are in the spec files and in the gate report, `cinematic_gate_r01_r03.md`.
