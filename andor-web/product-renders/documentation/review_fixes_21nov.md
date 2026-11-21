# Visual review (21 Nov): the four fixes

**The review** approved the visual direction and website integration, and asked for four fixes before publishing. This is what was done for each.

## 1. Campaign stills and the Framer kit on the 17 Nov interface

**The question.** The C3 campaign stills and Website Asset Kit v1 were rendered on the 17 Nov interface (`08ebd01`), before the beta freeze (`32e0bee`). Do any of them visibly differ from the shipping build?

**What differs between the two interfaces** (`interface_release_check.md`):
- the CORE / USER tab above the macro row;
- the macro arc colours: orange, blue and teal before; sage, ochre and umber now;
- the footer's new Library key.

**How it was checked.** Each still's camera was rendered twice, once on each interface, with nothing else changed:
- at about the size a website shows it: 1920 px wide for landscape, 1080–1170 px for portrait;
- 16 samples;
- then the two were compared after a 1.5 px blur, so render noise doesn't count. The threshold for a visible change is more than 12 levels.

**Results.** Sheet: `images/interface_audit_17nov_vs_beta.jpg`. Data: `interface_audit_17nov_vs_beta.json`.

| Still | Share of the frame that changes | What changes | Decision |
|---|---|---|---|
| A1-R1, A1-R1-W (desktop heroes) | 0.01–0.02 % | the Library key in the footer, a few pixels at web size | keep |
| A1M-R1 (mobile hero) | 0.02 % | footer key, macro arc colour, both a few pixels | keep |
| A2 gallery, and the transparent set (same camera) | 0.04 % | footer key, macro arcs | keep |
| B1-R4, B2 (dark, floating) | under 0.01 % | footer key | keep |
| M02, M03, M04, M05 (details) | 0 % | none: these crops don't include the macro row or footer | keep |
| **M01 macro knobs** | 0.18 % | **the arcs around FILTER and MOVEMENT: green and blue before, teal and ochre now**, in a close-up whose subject is these controls | **fixed (see below)** |
| **M06 touch strips** | 0.13 % | **a macro arc at the top edge: orange before, sage now**, soft but clearly coloured | **fixed (see below)** |

**What was done:**

| Image | Treatment |
|---|---|
| **M01 sharp, M01 cinematic** | **Re-rendered** on the frozen interface: the same shot, samples (80) and noise threshold (0.008) as C3, about 8 minutes each on the CPU. Against the approved masters, only the two arcs change (0.10–0.16 % of the frame after a 3 px blur); everything else reproduces. |
| **M06 sharp** | **The approved R2 master, with only the arc replaced.** Today's scene doesn't reproduce R2's sheen on the sensor glass exactly: its orange is more saturated, and the whole frame is about 2 levels darker, with the 17 Nov textures as well. A full re-render would have changed a look the owner approved. So the arc's pixels (1.2 % of the frame) come from the frozen-interface render, level-matched (+0.7 to +1.0 levels) and feathered over plain, out-of-focus panel. Every pixel outside the patch is unchanged. Before and after: `images/m06_sharp_arc_patch.jpg`. |
| **M06 cinematic** | **Unchanged**, the approved master. At f/5.6 the arc is a faint smear that the comparison does not detect. |

**Rebuilt from them:**
- the web derivatives (`renders/web_c3/`, the M01 and M06 sharp files);
- Website Asset Kit v1's Framer-upload and responsive files, and its `kit.json` byte sizes;
- the part 1 detail JPEGs. The ZIP is reissued as revision 2.

**Note for later re-renders of M06.** The committed scene no longer reproduces the R2 sensor sheen bit for bit. A future re-render of M06 should be compared with the R2 master before it replaces it.

The remaining stills differ only by details of a few pixels at website size, so re-rendering them is not worth it. If a still is ever used large enough to show the footer, re-render it then: A1 and A2 are about 25 minutes each on the CPU.

**Out of scope:** the R2 style frames also used the 17 Nov interface. They are superseded by the H01 R4 stills and the R3/R4 films, which all use the frozen interface. Don't publish them.

## 2. R01_PORTRAIT-R4-type-t1 encoded as H.264 4:4:4

**Cause.** The helper that encoded the assembled films put its settings after the output file name, so ffmpeg ignored them. Four films were affected, not one:

| Film | Problem |
|---|---|
| R01_PORTRAIT-R4-type-t1 | H.264 High 4:4:4 Predictive (most browsers can't play it), at default quality |
| R01_PORTRAIT-R4-matte | full-range YUV (`yuvj420p`) |
| R03_GLIDE-edit-15s5 | default quality (CRF 23, 1.6 Mbit/s) instead of CRF 14 (4.8 Mbit/s) |
| R01_CUT-R4-type-t1 | built from the CRF 23 portrait, so softer than the clean cut |

**Fixed.**
- All four were re-encoded from their sources: the PNG frames, or the CRF 14 master.
- All 13 films are now H.264 High 4:2:0, limited range, CRF 14.
- The T1 portrait matches the clean portrait's colour to within 0.2 levels.
- The motion ZIP was reissued as revision 2.

## 3. Web delivery versions

See `launch-kit/web/motion/README.md`.

**What was made:**
- H.264 at 854, 1280 and 1920 px, and AV1 at 1280 and 1920 px. The files are 0.3–4.2 MB, with SSIM 0.992–0.995 against the masters.
- Posters (the first frame) and representative stills.
- `motion.js`: lazy loading, play when visible, size and codec choice, and reduced motion / Save-Data handling (no download until the visitor presses play).

**Tested in Chromium:** desktop, reduced motion, and a phone without AV1.

**Still to do:** Safari and Firefox on real devices.

## 4. R03 flicker

**What the review saw.** R03's measured improvement was much smaller than R01's or D2's.

**What was checked:**
- Consecutive frames of the final, at native resolution, cropped on the B display (`images/r03_final_consecutive_frames.jpg`).
- **Result:** the particles and the waveform keep the same pattern from frame to frame, only shifted by the glide. There is no denoiser boiling.

**Why the number stays high:**
- The glide passes close to glass with depth, so parallax and highlights sliding across the glass count as change in any whole-frame measure.
- The screens themselves animate.
- On a display crop, the residual falls from 2.59 to 2.03 levels (−22 %, `r03_screen_flicker.md`).
- Rendering at 4K and downscaling gives no further gain (2.06).

**What can still show at playback.** The waveform's hairlines are 1–2 px apart at 1920, and may shimmer slightly as they slide. That comes from the texture's fine detail, not from noise.

**Recommendation:**
- Watch R03 at normal speed on a desktop screen and a phone before making it a prominent autoplay film.
- The 1280 and 854 web versions are downscaled, which averages the hairlines and lowers any shimmer.

**If it shimmers:**
- **The fix.** Re-render R03 with a wider pixel filter on the screens only: about half a GPU-hour, roughly $0.45.
- **The budget.** That needs new budget: $4.65 of the $5 cap is spent.
