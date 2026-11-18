# Gate D · Motion preproduction (Phase 4)

**Three restrained camera-move concepts, built on the approved master.**
- The product never moves.
- Only the camera does: slow eased orbits, tilts and push-ins around an approved shot's own focus point.
- Every frame renders the real scene, so shadows are always physically the scene's own. No shadow pass is moved under a moving product.
- No typography is baked in.
- Functional moments (playing, waveforms responding) use real plugin recordings, not the render.

**Previews only:**
- 640 × 360 (D3 at 640 × 800), 12 fps, 16 samples;
- made by `blender/scripts/motion_preview.py --plan D1|D2|D3`, which writes the frames, an MP4, the opening and closing frames, and `plan.json` (the camera path in metres);
- **final production starts only after review.**

**Timing:** the camera paths are in `motion_preview.py` (`PLANS`). Each move uses smoothstep easing, so it starts and ends at rest.

## D1 · The Instrument Reveal (light hero)

| | |
|---|---|
| Base | A1-R1 Warm Gallery: setup, light and composition as approved |
| Move | One continuous shot, about 9 s. The camera starts 10 % further away, 9° to the left and 4° higher. It settles exactly on the A1-R1 composition and finishes 3° past it to the right, as if a person walked a step along the instrument |
| Feel | Stationary, substantial, quiet. No 360° spin, no product rotation |
| Desktop | Hero background loop: play once, hold on the closing frame, which is the approved A1-R1 still. Text stays live in Framer on the left. The `A1_R1_W` framing applies to the wide-headline layout |
| Mobile | Same move, cropped from a 19.5:9 render of A1M-R1's framing (final production), or the A1M-R1 still |
| Reduced motion | The closing frame, which is identical to the A1-R1 still |

**Storyboard:**
1. **0 s:** wide and slightly high, the instrument entering from the right third.
2. **3 s:** the camera arcs in, the light rakes across the macros.
3. **6 s:** at the approved A1-R1 composition.
4. **9 s:** at rest, a touch to the right.

## D2 · The Material Sequence

| Cut | Base shot | Length | Move |
|---|---|---|---|
| 1 Timber junction | M05 (front-left corner, cinematic) | 3 s | Slow arc of 10°, a 6 % push-in |
| 2 Macro knobs | M01 (cinematic) | 3 s | Arc across the flutes (10°), a 4 % push-in: the highlight travels over the flutes |
| 3 Waveform display | M03 (cinematic) | 3 s | Arc of 8°, settles on the glass |
| 4 Keyboard | M04 (cinematic) | 3 s | Slide along the keys (8°), a 3 % push-in |
| 5 Touch strips | M06 R1 (cinematic; the corrected light) | 3 s | Arc of 8°: the satin sheen moves over the crown, PITCH stays orange |
| 6 Full instrument | A2 | 4 s | Arc of 6°, a 6 % pull to the approved A2 composition |

- **Feel:** tactile, light moving over material; hard cuts or short dissolves (edit's choice).
- **Total:** about 19 s.
- **Usage:**
  - desktop: a section-break film or product page;
  - mobile: cuts 1–5 individually as small looping tiles (each a 3 s loop, ping-pong).
- **Reduced motion:** the six M0x / A2 stills as a gallery.
- **Real UI:** the waveform cut shows the editor's still texture. If the film needs a moving waveform, that cut is replaced by a real screen recording of the plugin (see the website feature library), not animated in 3D.

## D3 · Floating Campaign (dark)

| | |
|---|---|
| Base | B1-R4: the dark architectural composition, its light and floating height |
| Move | About 8 s. A slow arc of 12° (−7° to +5°), a slight rise of 3° and a 4 % push-in |
| Shadows | The product floats over the real floor in the scene. Its shadow and the floor's reflections are rendered every frame, so nothing reveals an opaque silhouette |
| Desktop | Dark section or campaign page background |
| Mobile / social | 9:16 from B2_115_R4's framing (final production), for reels and stories |
| Reduced motion | The B1-R4 still (or B2 on mobile) |

## Final production plan (after approval)

| | |
|---|---|
| Resolution | 1920 × 1080 (desktop) and 1080 × 1920 (vertical), with 3840 for the hero if bandwidth allows |
| Frame rate | 24 or 25 fps |
| Samples | About 96 with denoising, temporally stable: fixed seed, no adaptive-noise flicker. Test 2 s first |
| Delivery | H.264 and HEVC/AV1 MP4 for the web (muted, `playsinline`), plus a WebM fallback, the poster frames and a reduced-motion still |
| Render cost estimate | At 1920 × 1080 / 96 spp on this 4-core machine: about 40–60 s per frame, so a 9 s D1 is about 2.5–3.5 h. A GPU render node is recommended for the finals |
