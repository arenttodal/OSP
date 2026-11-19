# ANDOR · Art Direction Gate R2: style frames

**Status:** for creative approval.
- Animation is paused.
- No external GPU cost has been incurred, and no 4K rendering has started.

**What this gate is.** Twelve art-directed style frames in four families (three compositions each), rendered at 1920 × 1080 with final lighting quality (128 samples, noise threshold 0.01, 16-bit). The Monument family also comes with the ANDOR wordmark composited behind the instrument, in two alternatives per frame.

**Where things are:**
- **Per-frame camera, light and stage configuration:** `art_direction_r2_frames.md`. It is generated from the specs, so it always matches what was rendered.
- **The specs:** `blender/cine/style/specs/`, written by `make_style.py`.
- **The package:**
  - the numbered contact sheet;
  - individual clean images (16-bit PNG and JPG);
  - the Monument mattes and typography composites;
  - `style_index.json`.

## What changed in the approach

The R01–R03 gate's problems were artistic: thin low angles, frontal endings, products low in frame, type above the product, the horizon line and uniform light. So the work moved from motion to stills, and from percentage targets to composition.

### Stage and light, as on a real set

The rig keeps every channel (independent camera, product, lights, focus and typography). It gains a `stage` block, which is additive: a spec without it renders exactly as before.

- **A seamless cyclorama.** Floor, a 1.4 m cove and the wall form one smooth surface, so there is no horizontal floor/background boundary. Used for Sculpture (B) and Immersion (D).
- **Lights placed per frame in the product's own frame:**
  - large directional softboxes;
  - strips;
  - a narrow pool (C2);
  - a low raking key for the D family.

  The setup's own lights can be removed or rebalanced per frame.
- **Black flags:**
  - *Negative fill:* real flags that absorb bounce, keeping the shadow side of the ivory in shadow (B).
  - *The display flag:* recomputed every frame where the display glass reflects the camera. The waveforms read clean instead of mirroring the studio. The reflections are removed through lighting, as on set, without touching the display shader.
- **Reflection cards that place their highlight** on a chosen point of the instrument. Linked to the instrument only, so a glossy floor never shows them.

### The wordmark

The correct brand wordmark is used: **ANDOR in Outfit ExtraBold at a 1.07 horizontal scale, no added tracking**. This is exactly how the instrument draws its identity (`shared/andor-ui/Source/InstrumentFrame.cpp`). The previous gate's Inter Tight is retired from this work.

How the composite works:
- The wordmark is composited behind the instrument through its rendered matte, so its soft edges occlude the letters exactly.
- The letters take the scene's light: full ink in the instrument's pool of light, falling off into the dark.
- Measured occlusion is recorded per composite.

### Kept as is

- **The instrument:** its geometry, materials and finishes are untouched.
- **The other renders:** the R01–R03 renders and all earlier assets render as before. The approved shot specs are unchanged.

## The twelve frames

Numbers match the contact sheet.

| # | Frame | Family | In one line |
|---|---|---|---|
| 01 | A1 Diagonal monolith | Monument | the instrument on walnut, one rising diagonal into the dark; the wordmark behind its far edge |
| 02 | A2 Plinth from above | Monument | a single bright plane against walnut and shadow |
| 03 | A3 Corner monument | Monument | the front-right corner as architecture, the far end into black |
| 04 | B1 Ivory three-quarter | Sculpture | seamless warm cyclorama, one low key, negative fill, long soft shadow |
| 05 | B2 Low profile, the curve | Sculpture | along the length: key fronts, knob skirts and the rounded edge against a gradient |
| 06 | B3 Overhead, long light | Sculpture | a graphic object in warm negative space, one low window light |
| 07 | C1 Timber, keys and macros | Silhouette | one strip skims the front-left corner; everything else is darkness |
| 08 | C2 A pool of light | Silhouette | a single pool on the macros and keys, the displays glowing beyond |
| 09 | C3 The outline | Silhouette | only a rim: the knobs' silhouettes and a hairline on the ceramic |
| 10 | D1 The three sound engines | Immersion | low over the source cards, the middle display sharp |
| 11 | D2 The macro row | Immersion | six knobs on a diagonal, long evening shadows |
| 12 | D3 Keys and expression | Immersion | from the left end: cheek, PITCH/MOD and keys, the surface rising behind |

The full rationale for each frame is in `art_direction_r2_frames.md`.

CRITIQUE_PLACEHOLDER

## Decisions carried from the review

- **R01:**
  - The current alternatives are archived as technical studies.
  - The next Grand Reveal will combine B's dark environment with A's coordinated motion.
  - R01 C is **not** to be pulled back to 80 %.
  - New reveal concepts: Monument, Sculpture and Arrival.
- **R02:**
  - A continues as the lighting concept, closer and more immersive. C1 and C2 here are its style frames.
  - C is a secondary material study.
  - B is not prioritised.
- **R03:** to be reworked as a path across the actual instrument. D1–D3 are its style frames. R03 C is to be considered as a physical-to-software transition.

## Next animation gate (only after these frames are approved)

1. **Grand Reveal: Monument.**
   - Opens in darkness, close on the walnut cheek.
   - The light arrives before the camera moves.
   - The reveal keeps developing to the last second: the instrument's diagonal is complete only at the end, and the wordmark is uncovered by the instrument's own silhouette.
   - The closing pose is the approved Monument frame.
2. **Silhouette: controlled travelling illumination.** C1's camera, with R02 A's travelling strip. The strip ends at the macro row, where C2's pool takes over.
3. **Immersive Flyover: the three sound engines.**
   - D1's height and light.
   - A path from the macro row over the three displays, with focus travelling with it.
   - Its closing pose is the possible handover to the software.

**Movement rules:**
- Each shot gets its own movement curve.
- No completed reveal at the midpoint.
- Meaningful opening, intermediate and final compositions.
- No fast cuts, particles, flares, generic technological effects or unnecessary rotations.
