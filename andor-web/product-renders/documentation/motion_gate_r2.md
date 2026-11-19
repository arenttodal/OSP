# ANDOR · Motion Gate R2: relit stills, B3 formats, four motion drafts

**Status:** draft motion, for review.
- No external GPU cost has been incurred.
- No final-quality motion has been rendered.
- The relit stills and B3 formats are rendered at still quality: 1920 px, 128 samples, noise 0.01, 16-bit.
- The motion is a **draft**: 768 × 432, 24 fps, 6 samples (denoised), 8-bit, silent, handles not included.
  - **Judge:** composition, movement, timing and light.
  - **Do not judge:** the noise, or the shimmer on the displays.

**Where things are:**
- **Shot parameters:** `motion_r2_shots.md`, generated from the specs.
- **The specs:**
  - motion: `blender/cine/motion/make_motion.py`, written to `cine/shots/R01_MON`, `R02_POOL`, `R03_ENG` and `R04_WIN`;
  - stills: `blender/cine/style/make_style.py`.

## 1. Relit stills

**B1 · Ivory three-quarter.** The broad key read the top surface evenly.
- It is now a small, low key raking from behind left, so the ceramic turns from light to shade across its width and every knob throws a short shadow.
- Two black flags hold the right half in true shadow: one close on the right, one overhead.
- A 6 cm strip draws the right cheek.

**D2 · The macro row.**
- **The hotspot:** the bright cyclorama behind the knobs is gone. The raking key now lights only the instrument, and the room behind falls into a dark, warm gradient.
- **Depth of field:** f/16 focused on the middle of the row keeps four to six knobs readable. It is not a single-knob macro.
- **A thin kicker** draws MOVEMENT's far rim.

## 2. B3 as a canonical asset: three formats

The palette and the directional window light are unchanged in all three.

| Format | What changes |
|---|---|
| Editorial | The approved B3 crop, unchanged |
| Complete, with negative space | The camera rises to 2.25 m. The whole instrument sits off-centre right, with its long shadow and a warm field to the left for copy |
| Vertical (9:16, 1080 × 1920) | Composed for vertical, not cropped. The instrument is turned 64° so its length runs down the frame on a steep diagonal, with warm space above for the wordmark or a line |

**The canonical configurations** are the specs, which are version-controlled and render the same every time:
- `SF_B3`, `SF_B3_full` and `SF_B3_vertical`;
- `SF_A2`;
- `SF_C2`.

## 3. The four motion drafts

Every shot has its own movement curve. The light arrives before the camera, and each shot keeps developing into its last third.

| Shot | Opening | Middle | Close | Curve |
|---|---|---|---|---|
| **R01_MON Grand Reveal: Monument** (12 s) | C1's darkness: the instrument's corner close by, a rim on the teak cheek, a strip on PITCH/MOD | The key and the long top sweep rise. Only then does the camera withdraw, climb and travel right while the instrument turns 30° → 0° about its centre | A2's three-quarter, with the **complete instrument**; the wordmark resolved beyond its far corner | Bezier (0.62, 0, 0.30, 1) from 1.6 to 11.6 s: long to leave, still travelling in the last third |
| **R02_POOL Silhouette** (9 s) | Darkness, a faint rim | A narrow strip crosses PITCH/MOD, the keys and the six macros. A pool begins to gather above the macro row | C2's pool, softened, the labels readable, the displays faintly lit beyond | The pool forms over 3.4–8.7 s and rises from 0.56 to 0.70 m as it brightens, so it widens as it forms. The camera only breathes (3 % closer) |
| **R03_ENG Immersive Flyover** (10 s) | Low over source A: its waveform, the ANDOR/OSP identity, the teak rail | B's display passes, with the source knobs soft in the foreground | C's waveform arrives as the move settles | A **lateral translation**: position and target move in parallel, so D1's direction of view never changes. Bezier (0.38, 0, 0.62, 1) |
| **R04_WIN The Window Light** (9 s) | B3 exactly | The camera sinks 10 %. The window drifts 24 cm along the wall and 4 cm down | The shadow has turned a few degrees and the sheen has moved across the ceramic | The camera eases (0.30, 0, 0.70, 1). The window moves linearly, because a sun does not ease. The instrument does not move |

### R01 typography

The clean master carries no type. Three separate wordmark passes are composited through the instrument's matte on every frame:

| Pass | Treatment |
|---|---|
| T1 | Readable, the far corner crossing its feet (A2's T1) |
| T2 | Monumental, cropped by the frame |
| T3 (new) | Between the two: the mark spans the frame and stays readable, while the instrument's far edge crosses its lower letters |

How the passes move:
- Each fades in and sharpens over 4.5–10.5 s.
- The instrument's coordinated turn and the camera's travel uncover it.
- The measured occlusion at each keyframe is in the package's `gate_index.json`.

**Measured occlusion: the share of the letters the instrument covers, from its rendered matte.**

| Pass | Opening | Quarter | Mid (6 s) | Three-quarter (9 s) | Closing |
|---|---|---|---|---|---|
| T1 | 100 % | 100 % | 97 % | 18 % | 16 % |
| T2 | 95 % | 96 % | 86 % | 47 % | 42 % |
| T3 | 86 % | 88 % | 65 % | 13 % | 11 % |

- **The uncovering is real.** The camera's travel and the instrument's turn uncover the mark between about 6 and 9 s, while the pass fades in over 4.5–10.5 s.
- **It settles early.** Most of the uncovering is done by 9 s, so the three-quarter and closing frames look nearly alike. If the reveal should still be travelling in the last 3 s, the next draft moves the curve's centre later.

**The metric was corrected in this gate.** It used to threshold the faded ink, so a pass reads 0 % while it fades in. It now measures the letter shapes themselves.
- The stills' occlusion values in `style_index.json` were re-measured the same way. For example, SF_A2 T2 is 42 % covered.
- No published figure quoted the old values.

### Review notes on the drafts

- **R02_POOL.** The transformation builds steadily:
  - The strip crosses the controls in the first quarter.
  - The pool is still partial at mid.
  - The displays stay dark until the three-quarter frame.
  - The closing pool reads fully lit across the macro row. If the centre is still too bright, the next step is a further −0.3 EV on the Pool light only.
- **R03_ENG.** The track passes A, B and C in order, with the direction of view constant. At f/8 the near source knobs are soft, which carries the depth of D3. The engines' displays stay sharp.
- **R04_WIN.** It is deliberately almost still. The change is in the shadow edge and the sheen, so judge it as a moving clip, not from the keyframe sheet.

### Performance preparation

**R03_ENG and R04_WIN keep the displays and controls free.**
- The display flag follows the camera, so no studio reflection crosses a waveform.
- The waveforms and knobs are static here. The performance versions will drive them from Virtual Studio frames: the same cameras and lights, with the textures swapped per frame.

## 4. Model consistency

**Timber.** The OSP master renders **teak**: `andor_scene.py --finish refined --teak warm`, the natural teak of Gate B, and the strip texture `teak_strip.png`, sha256 `cb7ecafcb641…`.
- Several of my Art Direction R2 and R01–R03 texts wrongly called the instrument's cheek "walnut". They are corrected to teak.
- "Walnut" now appears only for the dark **plank floor** of the FLOATING backdrop: "peat and walnut planks" (Gate C1). The instrument never uses walnut.
- The OSP / ANALOG material distinction is untouched.

**Interface.** The master's face texture (`textures_current/`, sha256 `f13d38ea…` / `e2a8abc7…`, matching the asset manifest) is the OSP editor from the default branch at **`08ebd01`**, captured on 17 Nov (Gate C2.5).

Since then, the default branch has 20 commits touching `shared/andor-ui`. Some change what the face shows:

| Commit | Change |
|---|---|
| `c010a8f` | CORE macros: the harmonised palette, with re-rendered beads |
| `d70b909` | ARP STRUM: a STRUM key beside SATELLITE |
| `0719b91` | The menu visual refinement |

**That is a specific inconsistency: the marketing model may not show the release interface.**
- I have not changed the model or its textures: you asked that approved geometry and materials stay untouched, and this needs your decision on which commit is the release interface.
- A re-sync is the Gate C2.5 procedure: a local capture of that commit, then `prep_textures.py`. It replaces only the face texture.
- It should happen before any final-quality motion is rendered.

## 5. Rig changes in this gate

All changes are additive.
- **Fixed:** a light added by a spec and also animated by it was captured before Blender had updated its transform, so it rendered from the origin.
  - It affected only the new motion specs; every earlier render's lights were unaffected.
  - The rig now updates the scene before capturing the lights.
- **Logged:** the rig records every light's actual energy per frame in each render's metadata.
- **Assembly:** `assemble_gate.py` composites spec-defined typography passes (Outfit wordmark, layouts from `monument_layouts.json`) and writes the matte preview.
