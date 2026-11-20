# ANDOR · Motion Revision R3 and the Front-Facing Hero (H01): production gate

**Status:** drafts for review (20 Nov).
- No external rendering cost has been incurred, and no final-quality motion has been rendered.
- Approved physical materials are unchanged.
- **Interface:** every render in this gate uses the release interface, `textures_0.9.0-beta.1` (ANDOR OSP 0.9.0-beta.1, `32e0bee`). Each render's metadata records it in `interface_textures`; all 13 metadata files were checked.

**Quality.**
- **H01 stills:** still quality, 1920 × 1080, 128 samples.
- **Motion:** draft, 768 × 432, 24 fps, 6 samples (denoised), silent.
  - **Judge:** composition, movement, timing and light.
  - **Do not judge:** the noise, or the shimmer on the displays.

**Tracking.** Each item below is a sub-item of the production gate in Linear, with its review media attached: ARN-7 (H01), ARN-8 (R01), ARN-9 (R02), ARN-10 (R03), ARN-11 (R04), ARN-12 (B1) and ARN-13 (D2).

**Specs:**
- `blender/cine/hero/make_h01.py` writes `cine/hero/specs/H01_A`, `H01_B` and `H01_C`.
- `blender/cine/motion/make_motion_r3.py` writes the motion specs to `cine/shots/`.
- `launch-kit/typography/compose_h01.py` with `style/h01_layouts.json` composites the H01 typography.
- `cine/assemble_gate.py` assembles the package.

## H01 Front-Facing Monument

The instrument faces the viewer: the keyboard edge is horizontal, with no yaw or roll and equal cheeks, seen from 32° above with a 90 mm lens. The ANDOR wordmark stands centred behind it.

| Variant | Wordmark | How much of the letters the instrument covers |
|---|---|---|
| **A** Dark Theatre | Warm grey ink on charcoal, fading toward the floor | 13 % |
| **B** Ivory Gallery | Multiply ink on the ivory cyclorama | 13 % |
| **C** Desktop website | Smaller and lower; headline and call to action in the free top band | 4 % |

- **C's headline and call to action are placeholders:** "One sound. / An entire instrument." and "Join the beta". The headline ends at 21.5 % of the frame height, and the button sits at 25–31 %.
- **Deliverables at this gate:** the clean still, the typography composite and the matte for each variant, plus C's website mock-up.
- **H01 B dolly preview (10 s, draft):** a 4 % dolly back that settles on the still, with the cove light easing into place. The wordmark stays fixed while the instrument recedes, so the covered share falls from 15 % to 13 %. The opening and closing frames are in its keyframe sheet.
- **Not started, as the brief asks:** the final H01 motion (after the still is approved) and the native mobile composition (only if C is approved).

## R01 · the two-composition cut

The rotation is gone. The shot is two compositions joined by a clean cut, 12.1 s together:
1. **Close, drifting (5.5 s):** a 3 cm linear sideways move at f/6.3 on the source cards and macros.
2. **The A2 portrait with the wordmark (6.5 s):** a 1.5 % dolly back that settles.

| Typography pass | How much of the letters the instrument covers |
|---|---|
| T1 | 16 % |
| T2 | 43 % |
| T3 | 11 % |

**Review.**
- Both compositions hold as stills.
- The drift in the close shot is barely visible, as intended.
- The cut lands on the portrait's opening frame, so no motion carries across it.

## R02 · the light reveals, then falls back to dim

The camera is fixed for all 11 s. The strip light rises and peaks at about 6 s, the pool and display glow follow, and everything falls back to the opening level by 10.6 s.

**Loop test.** The first and last frames are identical: the mean difference is 0.0 levels. Brightness (mean luminance, 0–255) runs 34 → 105 at the peak → 34.

**Notes for review.**
- The dim level holds for about 0.4 s at the loop point, frames 281–289. When looping, drop the last frame: it duplicates the first.
- The displays are dark at the dim level, by design: their glow follows the light.
- **A noise step at the loop point.** In the frame where the last light reaches exactly zero, the noise pattern changes (a mean difference of 1.2 levels with no change in brightness). The same happens when the light first comes on. This is a draft-sampling effect. It goes to the flicker investigation (ARN-3), and a floor value just above zero would remove it at final quality.

## R03 · the constant slow glide

A linear move of 17 s along the displays and controls at one constant speed, with no rotation or easing. Three overlapping sections are cut as separate files:

| Section | Time |
|---|---|
| A | 0–6.5 s |
| B | 5.5–11.5 s |
| C | 10.5–17 s |

**Note for review.** Section C ends past the instrument's right edge, against the dark part of the room. It is fine as a handle. If the full take should stay on the instrument, trim C to about 15.5 s, or shorten the move by 2.5 cm.

## R04 · Window Light at 16 s

R04's keyframes are stretched by 16/9 in the source animation and every frame is rendered fresh, with no frame interpolation. The overhead composition and the moving window light are unchanged.

## B1 · Three Displays, lateral glide

A 7 cm sideways glide over 12 s, eased and centred on the approved still.

**Does not yet meet the brief.** At the opening, the reflection on display C (green) washes out about its right third. It shrinks as the camera moves but still crosses that display at the end.
- **Proposed fix:** flag the reflected source (or lower it) so the glass keeps a narrow edge highlight, then re-render the 289 draft frames, about 25 minutes.
- Not done before review, so you can judge how strong the reflection should be.

## D2 · Macro Row, lateral track

A 3.5 cm track over 12 s, centred. The focus travels with the camera, so the focus distance is constant and it does not hunt. Four to five knobs stay sharp throughout.

## Next, after review

- Revise any shot with notes.
- Render the final H01 motion and, if C is approved, the native mobile composition.
- Run final-quality renders, after the cost is approved (ARN-3).
