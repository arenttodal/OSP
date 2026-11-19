# First creative gate: R01 Grand Reveal, R02 Silhouette, R03 Above the Sound

**Scope:**
- Three signature shots, each with three alternatives, plus the R01 desktop website composition.
- Each comes with:
  - a narrative;
  - opening, quarter, mid, three-quarter and closing frames;
  - a full-motion draft;
  - its camera, object and light parameters (`cinematic_gate_shots.md`, generated from the specs);
  - a high-quality representative still.
- R01 adds a typography-composited preview, the clean master, the product matte, and for R01 A, the background pass.

**What a draft is.** Every motion file here is a **draft**: 768 × 432, 24 fps, 6 samples (denoised), 8-bit PNG intermediates, silent.
- **Judge:** composition, camera choreography, timing and light.
- **Do not judge:** the noise, the fine texture, or the 1-frame shimmer on displays. Those are draft-sampling artefacts; finals use the 0.01 noise threshold.
- **Not included in drafts:** handles. The rig renders at least 1 s of handle at each end for finals.

**Stills:** 1920 × 1080, 128 samples, noise 0.01, 16-bit. This is final-quality light at half the master width.

## How to review

**Package:** `gate-R01-R03/` (see the delivery message).
- **Folders:** one per shot family, then one per alternative.
- **In each alternative's folder:**
  - `…_draft_v01.mp4`: the full motion;
  - `…-keyframes_….jpg`: the five frames on one sheet;
  - `keyframes/`: the five frames at full draft size;
  - **R01 only:** `…-typography_…mp4` (the wordmark composited behind the instrument through the matte) and `…-typography-keyframes_….jpg`;
  - **R01 A only:** `…-background-pass_…mp4` (the environment without the instrument).
- **Stills:** in `stills/`.

## R01 Grand Reveal (11 s)

**The move.** Each alternative:
- opens low, oblique and close, with the instrument wider than the frame;
- withdraws in one breath on the house curve: slow start, still moving at three-quarters, quiet arrival at 10 s;
- tilts the instrument toward us about its own centre: 18° in A, 22° in B, 20° in C;
- settles low in frame for a 1 s hold.

**The wordmark** is placed from the closing frame, so it ends fully readable above the instrument. During the move it is:
- discovered in the defocused wall: blurred, then sharpening as the camera withdraws;
- faded in over 3–40 % of the move, so it is seen while the instrument still covers part of it;
- uncovered as the instrument descends (in A and C the instrument covers the lower part of the letters until about 40 % of the move);
- printed on the wall: the ink darkens the wall's own light, so light gradients pass through the letters.

Occlusion is measured on every frame and stored in `typography_meta.json`.

**B is the exception.** In the DARK THEATRE the instrument stays below the wordmark for the whole move (measured occlusion 0 on every frame), so in B the wordmark rises out of darkness rather than out from behind the instrument. To get a real occlusion in B, the opening camera would have to be raised; that is an option for review, not a change made here.

| | A: ivory editorial | B: dark sculptural | C: architectural |
|---|---|---|---|
| Stage | IVORY GALLERY | DARK THEATRE (B1/B2 preserved) | IVORY GALLERY, low camera |
| Lens | 65 mm, f/7.1 | 75 mm, f/8 | 50 mm, f/8 |
| Opening | Across the timber cheek and the first card | A rim along the front edge, just above eye level | Almost at the instrument's level: a landscape of controls |
| Closing width | about 77 % | about 72 % | about 85 % |
| Light | The key softbox orbits −14° → +4° as the instrument turns | Key, top line and fill rise from near-black | The key orbits +10° → −4° |

**A desktop.** R01 A with a wider, right-weighted end frame: the instrument is at about 55 % width on the right. The headline "One sound. An entire instrument." (EB Garamond) arrives in the free left column after the wordmark. Its size is fitted from the matte, so it can never meet the instrument.

## R02 Silhouette (8 s)

**Light reveals; the camera does not move** (or moves imperceptibly). Displays stay dark until the light has done its work, through the rig's emission channel. There is no glow.

| | A: travelling strip | B: the light rises | C: highlight scan |
|---|---|---|---|
| Stage | DARK THEATRE | IVORY GALLERY at dusk | IVORY GALLERY, dimmed |
| Camera | Locked, 85 mm, low three-quarter | 70 mm, a 2.5 cm push over 8 s | Locked, 85 mm, three-quarter above |
| Reveal | A rim line, then a strip card travels along the front: curvature, walnut, then controls | A low raking light swings round, then the room's soft light rises | A broad card, seen only in reflection, glides across the surface; each knob, key and bezel flares as it passes |

## R03 Above the Sound (9 s)

**Medium-wide**, no extreme close-ups, f/11.
- **The frame:** the three source sections, their displays and source controls, and part of the macro row.
- **The versions:** this is the static-display version. The performance version uses the same camera with Virtual Studio frames on the displays and turning knobs.

| | A: diagonal flyover | B: lateral glide | C: crane forward |
|---|---|---|---|
| Stage | IVORY GALLERY | DARK THEATRE | IVORY GALLERY |
| Move | Near-left corner to far-right, rising 22 cm | Right to left, descending 14 cm; the camera leads its subject | From low front across the keys, up and over the macro row |

## Rig changes found while calibrating

- **The product frame was wrong in the DARK THEATRE.** It counted the backdrop's floor planks, which put the centre 8 cm low. Fixed: the frame is now measured from the instrument only.
- **New channels:**
  - `world` and `emission` multipliers, so near-darkness is real and displays do not lead the reveal;
  - 8-bit draft frames (16-bit stays for stills and finals).
- **Display reflections:** at very low camera heights the displays mirror the bright wall (visible briefly in R01's quarter frames and R03 C's early frames). This is physically correct and transient. To be judged in review; a negative card can kill it in finals.

## Production estimate (finals, after approval)

**What a final is:** 4K UHD masters (3840 × 2160), 24 fps, 1 s handles, noise threshold 0.01, 16-bit EXR or PNG, plus beauty, matte, clean and shadow passes.
- **Per approved alternative:** R01 is 13 s (312 frames), R02 is 10 s (240) and R03 is 11 s (264): 816 frames for one alternative of each.
- **Derivatives:** 1080p and 2K come from the masters, with no extra rendering.

**On this machine** (4 CPU cores, no GPU):
- About 7 min per 1920-px frame at final settings (measured in the Gate D2 quality test), so about 28 min per 4K frame.
- That is about 380 hours for the beauty pass of 816 frames. Mattes add about 2 % and clean passes about 60 %.
- Not practical.

**On a GPU:**
- The benchmark bundle (`blender/benchmark/`, about 6 MB with checksums) runs the same frame with `--device OPTIX` or `CUDA` and checks colour parity against this CPU reference (mean shift under 0.5 levels).
- No GPU benchmark has been run: none is available here.
- A single current high-end GPU is commonly 20–40× faster than this 4-core CPU on Cycles. That would be about 10–20 GPU-hours for the 816 beauty frames, plus passes. **This is an estimate to be replaced by the benchmark's measured number.**
- **No external rendering has been started or paid for.** Running the benchmark on a rented GPU, and any production render, needs your approval of the provider and the cost first.
