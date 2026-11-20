# ANDOR · Gate R4: refined H01 stills, R01 T1 cut, R02 lower light, B1 study, frozen interface, GPU benchmark

**What this gate is.** The R3/H01 creative review (20 Nov) approved the restrained cinematography direction. This gate returns only what that review asked for. There is no new camera system and no broad set of alternatives.

**Interface.** Every render here uses the frozen release interface (`textures_0.9.0-beta.1`, approved commit `32e0bee`, verified identical at the beta head `6ef3376`).
- `cine_rig.py` refuses any final-quality render whose textures don't match `FROZEN.json`.
- Every render records `interface_frozen` and `interface_commit` in its metadata.
- See `interface_release_check.md`, section *Freeze (R4)*.

**Quality:**
- **H01 stills and the B1 study:** still quality, 1920 × 1080 at 128 samples.
- **Motion:** draft quality, 768 px, 24 fps, 6 samples (denoised).
- **R02 loop check:** final quality, 1920 px at 96 samples.

**Not done at this gate, as the review asked:** the final H01 animation (it waits for the still's approval), and the full 4K batch (it waits for the owner's approval).

**Tracking:** Linear ARN-30, the gate, with sub-items ARN-7 (H01), ARN-8 (R01), ARN-9 (R02), ARN-10 (R03), ARN-12 (B1), ARN-31 (interface) and ARN-3 (GPU).

## H01 Front-Facing Monument: refined still compositions

**What is kept:** the approved geometry (square-on, no yaw or roll, 32° elevation, 90 mm lens) and the physical design.

**Specs:** `cine/hero/make_h01_r4.py` writes `hero/specs_r4/`. The R3 specs are unchanged.

| | R3 | R4 |
|---|---|---|
| Camera distance (same lens) | 1.75 m | **1.385 m** |
| Instrument's share of frame width (A, B) | 61 % | **78.8 %** |
| Instrument's centre (fraction of height from top) | 0.71 | **0.61** |
| Back edge (fraction of height from top) | 0.51 | 0.355 |
| C: instrument's share of width | about 45 % | **67 %**, in the lower two thirds |

**Light, for more depth in the ceramic, timber and controls:**
- **The key** moves further back and narrows, so the surface falls off from the back edge toward the keys and reads as a receding plane.
- **In A:** the side rakes come lower and slightly behind, so each knob throws a longer shadow and its fluted skirt catches a rim. The flat front fill is reduced to about a quarter.
- **In B:** the low left rake is stronger, and a narrow rim from behind-right draws the right cheek. The two flags come closer, so the timber cheeks hold their shade against the ivory.

**The wordmark** (`h01_layouts.json`, keys `H01_A_R4` and `H01_B_R4`):
- Cap height 0.30 of the frame puts it at 91 % of the frame width, a little wider than the instrument, so the A and the R stand clear of the cheeks.
- The instrument's back edge covers the bottom fifth of the letters: **14.9 %** of the letter area. The occlusion is genuine, and the word still reads in full.

**C as a website plate.** No baked-in headline, call to action or wordmark: they would compete with the page's own content.
- The headline, the line and the button are **native HTML** over the plate: `launch-kit/web/h01_c_hero.html`, using the site's own fonts and tokens.
- The mock is screenshotted at 1440 × 900 and 1920 × 1080. The plate is cropped to cover at both, and the instrument stays whole.

**Deliverables:** for each of A, B and C, the clean still (16-bit), the matte, and the typography composite (A, B) or the website mock (C).

## R01: the T1 cut

**What is kept:** the two-shot structure, the close lateral move and the cut to A2. The cut uses T1, the preferred typography.

**Specs:** `cine/motion/make_motion_r4.py`.
- **The destination.** The R3 portrait cropped the instrument's front-right corner and keyboard end. R4 eases out to 1.10× A2's distance, with the same 1.5 % settle, so **the whole instrument** sits in frame. The scale step across the cut stays about 2×. The instrument now covers **12 %** of the T1 letters (it was 16 %).
- **Tonal continuity.** The ceramic was already matched across the cut (median 211 against 209 out of 255). What jumped was the frame: wall-to-wall lit ceramic (mean 135) cut to the dark theatre (mean 69).
  - The close shot is now framed 9 cm further back on the instrument, so its far edge and the theatre's dark run across the top of the frame, as they do in the portrait. The close frame's mean falls to about 110.
  - The exposure, the lens and the 3 cm drift are unchanged.

**Deliverables:**
- `R01_CUT-R4-type-t1`: 288 frames = **12.0 s** (132 + 156);
- the clean cut;
- each shot on its own, the portrait's matte, and keyframe sheets.

## R02: the lower peak

**What is kept:** the return to darkness, the movement, the timing and the dim level.

**What changes:** the strip, the pool and the display glow peak **0.3 EV lower** (× 0.812).
- On screen, the peak frame's mean brightness goes from 103 to 99 out of 255: the AgX view transform compresses highlights, and the rim and the world light are unchanged.
- The visible difference is in the highlights: the white keys and the ceramic under the pool keep their texture instead of flattening.

**Loop, at final quality** (1920 px, 96 samples, noise 0.01, frames 25 and 289, for R3 and for R4): see *Loop verification* below.

**Deliverables:**
- the R4 loop, 264 frames = **11.0 s**, which loops with no repeated frame;
- R3 and R4 side by side (264 frames);
- keyframe sheets.

## R03: the preferred 15.5 s edit

**What is kept:** the constant-speed glide and the full source take.

**The edit ends at 15.5 s,** before the camera loses the instrument's right edge: display C and the teak cheek are still in frame there. After that the instrument drifts out and the dark room takes over.

**Deliverables:** the edit (372 frames = 15.5 s) and the source take (408 frames = 17.0 s), cut from the approved R3 render. Nothing was re-rendered.

## B1: a closer study, and the full-product glide archived

**Decision:** the R3 B1 glide is **archived as supporting full-product footage**. It won't be final-rendered as a three-display film.

**The optional study:** one closer medium-wide still from the approved B1 still's direction (`B1_CLOSE_STUDY`).
- **Framing:** 0.68× the distance, aimed at the display row. All three displays and their control rows fill the frame, and the controls stay recognisable.
- **Reflections:** the strip that washed out display C in R3 is now low and narrow (7 W, 5 cm), so the glass keeps only a thin edge highlight and all three waveforms read.
- **No duplicate:** it repeats neither a hero shot nor D2. It is a still for approval; no glide has been rendered from it.

## Frozen interface

See `interface_release_check.md`, *Freeze (R4)*. In short:
- the beta head `6ef3376` is pixel-identical to `32e0bee` in the hero, and its textures are byte-identical;
- the three new commits change only menus, the ARP card and a granular display, none of which appear in the model;
- the texture set is frozen and enforced.

## GPU final-quality benchmark and estimate

See `gpu_benchmark_r04.md`. In short:
- R04 at 4K, final quality, on a Runpod RTX 4090: **12.0 s per frame** with GPU denoising (54.5 s with CPU denoising). Parity with the CPU passes.
- **All approved shots at 4K:** about 2,200 frames, or about 7.4 GPU-hours, **about $8–9 on Secure Cloud**, ±50 % by shot.
- **Spent:** $0.99 of the $5 cap. Both pods are terminated.

## Frame counts: corrected

The renderer writes a shot's closing frame at exactly t = duration, so every R3 preview carried **one extra frame**. For example, a 12 s shot had 289 frames (12.04 s), and the R01 cut had 290 frames (12.08 s, documented as "12.1 s").

**From R4 on, every preview is exactly duration × 24 frames.** The extra closing frame is kept only as a PNG, where a still is wanted.

| Shot | Documented | R3 delivered | R4 |
|---|---|---|---|
| R01 cut | 12.1 s | 290 frames (12.08 s) | **288 (12.0 s)** |
| R02 | 11 s | 265 (11.04 s), first frame repeated at the end | **264 (11.0 s)**, a seamless loop |
| R03 source take | 17 s | 409 | **408** |
| R03 edit | (new) | | **372 (15.5 s)** |
| B1, D2 | 12 s | 289 | 288 at final |
| R04 | 16 s | 385 | 384 at final |
| H01 dolly | 10 s | 241 | 240 at final |

## Loop verification

(To be completed when the final-quality loop frames finish.)
