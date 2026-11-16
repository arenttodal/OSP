# Gate C · Checkpoint C2: commercial image refinement (16 Nov 2026)

These are targeted refinements of the approved C1 compositions. The model, materials, knobs (G, 0.30 D), the 14.4 mm teak,
the keyboard and the front return are unchanged. Every change is in `blender/config/campaign_setups.json` or is an opt-in
switch in `andor_scene.py`.

## Technical and creative changes

| Image | Change | Why |
|---|---|---|
| **A1** desktop hero | Stone `0.62/0.595/0.555 → 0.585/0.556/0.512` (a touch darker and warmer). New **low rake** light (1.2 × 0.3 m, from the front left, 30 cm high, 4.5 W) grazes the lower controls, the macro knobs' skirts and the keybed. Front kicker 6 → 7.5 W. Edge strip spread 60° → 120°. Camera and composition unchanged | The ivory separates from the stone without added contrast. The lower controls and the key fronts read. The narrow spread drew a hard light edge on the stone in tall frames |
| **A1M** mobile hero (new) | Its own camera: 9:16 (1170 × 2080), 85 mm at 1.15 m, product turned −28°, pitch 41 (50 in the first render; see below); copy at the top 8–30 % | Composed for the phone, not cropped from desktop. A wider gallery floor (30 m) means no frame ever sees its edge |
| **A2** product gallery | A high-resolution master: 3600 × 2400, 160 samples, adaptive noise threshold 0.005 (C1: 56 samples, 0.012) | Fine GUI text, waveforms, flutes and key fronts stay sharp at 100 %, with less left to the denoiser |
| **B1** dark ad | `product_light_ev −0.3`: the key, rim, top line, kicker and fill are each × 0.81. **The floor lights are unchanged** | The ivory reads dimensional, not luminous. The screens and the teak highlights keep their level. The 100 mm float and the shadow are unchanged |
| **B2** vertical | Three projected-size studies at 9:16: current, +15 % (camera 1.36 → 1.18 m, product −26°) and +25 % (1.09 m, recomposed at −34°). Copy area moved to the top **14–34 %**, clear of the platforms' top UI; the bottom 20 % kept free for the call to action | +25 % still touches a cheek even when recomposed, so **+15 % is recommended**. It is clearly dominant on a phone with nothing cropped |
| **C1S** sharp macro (new) | Same light and angle as C1; f/14 at 0.20 m | The pointers, flutes, arcs and the CHARACTER / MOVEMENT labels are inspectable. The keys stay soft. C1 (f/8) stays the cinematic version |
| **Typography** | The real logotype: **ANDOR** Inter Tight 760, +0.02 em tracking · **/** 300 muted · **OSP** 300 (as the site's `.wordmark`); the line in EB Garamond. Contrast is measured against the pixels behind each line (`type_contrast.json`). The secondary colour is used only where it reaches 4.5:1, otherwise the primary | C1's layers used plain "ANDOR OSP" and didn't check the grey serif on the stone. The headline is the approved one; no new claims |
| **PITCH / MOD** (test only) | `--wheels physical`, from the approved GUI's measured shapes. Each wheel is an ivory bezel 0.6 mm proud, printed with the GUI's own bezel art. Inside, a 5 mm slot lined in charcoal holds an 8 mm-radius charcoal wheel, its crown 0.35 mm below the bezel, with two thin flanges (the GUI's guide lines) and, on PITCH, an orange satin band over exactly the GUI's orange length | It's isolated: not used in any campaign image and off by default. It's an interpretation for review before any extreme close-up. The plugin is unchanged |

## Inspected at 100 %

Checked for blur, reflections, texture artefacts, washed-out labels and clipping.

| Image | Finding | Action |
|---|---|---|
| **A1** | The GUI text, waveforms, flutes and key fronts are sharp. The ivory separates from the stone, nothing clips, and there are no fireflies | None |
| **A1M** | The first render (pitch 50) showed a hard light edge across the top 10 %, inside the copy area. It is the Edge strip's plane: the strip sits 35 cm off the floor behind right, so the stone just in front of it is bright and ends abruptly at its plane. Light fixes were drafted and rejected. Linking the strip to the product removes the edge but darkens A1's stone by about 30 %. Spread 180 only softens it | **Camera only:** pitch 50 → 41 (slightly more top-down), A1's lights unchanged. The top 2 % keeps a gentle natural falloff, above the copy area. Re-rendered |
| **B1** | The ivory reads dimensional, not luminous, and the floor is unchanged. At 1:1 the smallest printed labels (the subtitle, the knob captions) are faint. That is the GUI's own light grey on ivory, softened by the darker key. They hold at ad sizes | Noted. A further reduction would trade away the ivory's glow |
| **B2 +15 %** | The product is clearly dominant with nothing cropped. The copy area is on clean dark floor | None (+15 % recommended) |
| **C1S** | The pointers, flutes, value arcs and LEDs are crisp at f/14. CHARACTER / MOVEMENT sit just behind the focal plane: legible, slightly soft | None. C1 (f/8) remains the cinematic version |
| **PITCH/MOD test** | PITCH reads physical: the bezel, the slot, the wheel's crown and the orange band. MOD reads as a dark void, because a charcoal wheel in a charcoal slot catches no highlight at this angle | If adopted: a satin highlight on MOD's crown, or a slightly lighter wheel. Still a test; no campaign image uses it |
| **A2 master** | At 3600 × 2400 the smallest captions (START, TUNE, 0.0 dB), the waveform particles, the flutes and the key legends (C4–C7) are sharp. Nothing is smeared by the denoiser and nothing clips | None. Ready as the gallery master |

## Files

`renders/review_gate_c2/`:

- `00_contact_sheet`;
- `02_A1_desktop_mobile` (with type layers);
- `05_B2_scale_studies`;
- `07_before_after`;
- `08_pitch_mod_test`;
- the individual JPEGs, the `*_type_layer.png` files and `type_contrast.json`.

Editable scenes: `andor_gateC2_ANDOR_GALLERY / FLOATING / MATERIAL.blend`.
