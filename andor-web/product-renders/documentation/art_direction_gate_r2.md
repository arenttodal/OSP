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
| 01 | A1 Diagonal monolith | Monument | the instrument on the dark plank floor, one rising diagonal into the dark; the wordmark behind its far edge |
| 02 | A2 Plinth from above | Monument | a single bright plane against the dark plank floor and shadow |
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

## The Monument typography

There are two alternatives per frame. Occlusion is measured from the matte.

| Frame | T1: readable wordmark | T2: monumental, cropped by the frame |
|---|---|---|
| A1 | the far edge cuts the letters' feet (35 %) | the instrument laid across a mark larger than the frame (50 %) |
| A2 | the mark rises in the dark beyond the instrument's corner (16 %) | 82 %: abstract |
| A3 | the far edge crosses the middle letters (31 %) | 66 %: abstract |

**Recommendation: T1.**
- A1 T1 is the strongest interaction. The wordmark stands behind the object rather than above it, and the object's edge does the occluding.
- The T2 crops are striking as graphic texture. But they stop reading as ANDOR once more than about half the mark is covered. They are better kept for motion, where the mark can be uncovered.

## Critical assessment

### Strongest

1. **08 · C2, a pool of light on the controls.** The most cinematic frame here, and the one most clearly "ANDOR":
   - The light is the subject.
   - The instrument is shown where it is played.
   - The displays glow beyond the pool without leading it.

   It needs no typography, and it animates naturally: the pool travels or grows. This is the best bridge from R02 A.
2. **06 · B3, overhead, long light.** The most iconic still: a calm, graphic object in warm negative space, with one long shadow and clean displays. It would hold as a poster, a box or a homepage at any size. Its weakness is that it is static by nature: it is a photograph more than a shot.
3. **02 · A2, plinth from above.** The best Monument without type. A single bright plane on a strong diagonal against the dark plank floor, with mass and darkness on both sides. With T1, the wordmark sits naturally in the dark space.

   **01 · A1** is the better typography frame. Of the three Monument endings for the Grand Reveal, A1 and A2 are the candidates.

### Weakest

- **04 · B1, ivory three-quarter.** The most conventional frame and the closest to a catalogue three-quarter. The key is still too broad, and the top surface reads evenly. If B is kept, it should be relit, not re-angled: a smaller, lower key and a stronger negative fill. B2 and B3 already carry the family.
- **11 · D2, the macro row.** The long knob shadows are right. But the cyclorama behind blooms bright at top right, the foreground keys are too soft, and there is no single focal point. It needs a darker background and a clear hero knob. As it stands, it is a study, not a frame.
- **03 · A3, corner monument.** Confident scale, but the instrument fills the frame and is lit too evenly, so it reads as an interface shot more than a sculpture. A narrower key, so the far end falls away, would rescue it.
- **07 · C1** has the right mood but a very narrow focal plane: the mid surface goes soft. It works as the opening pose of the Silhouette shot, where the strip will travel. It is weaker as a still.

### Across the set

- **The ivory reads best when the light is small and low** (B3, C2, D1, D3) and worst when it is broad (B1, A3).
- **Darkness works when it is shaped.** The dark frames read as dark, not underexposed, because each has one controlled source and black flags keeping the rest black.
- **The flags work.** No studio reflection crosses a waveform in any frame.
- **The approved materials and geometry are unchanged.** Every improvement here is camera and light.

### Recommendation for the next animation gate

1. **Grand Reveal: Monument.** Ends on A1 with T1 (or A2), opening in C1's darkness.
2. **Silhouette.** Travels from C1 to C2.
3. **Immersive Flyover.** Starts from D1's height and light.

B3 is worth keeping as the campaign's key still, independent of motion.

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
   - Opens in darkness, close on the teak cheek.
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
