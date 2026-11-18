# Gate C3 · Final production (17–18 Nov 2026)

**What this is:** the approved decisions rendered at full resolution.
- A1-R1 Warm Gallery, A1M-R1, A2, B1-R4, B2_115_R4;
- material sharp and cinematic;
- touch strips B (2 % crown, 0.4 mm bezel);
- the model synced to the current OSP editor.

**No typography is baked in:** no headline, nav or CTA appears in any master. Layout guides stay in `blender/config/campaign_setups.json` for Framer.

**Where things are:**
- review sheets: `renders/review_gate_c3/`;
- web files: `renders/web_c3/`;
- the 23 PNG masters (535 MB) are in the Package A zip, not in git.

## 1. Master integration

- **Touch strips:** the PITCH and MOD touch strips are the default (`--wheels strip`, crown 0.02, bezel 0.4 mm) in every campaign scene.
- **Wheels removed:** the mechanical-wheel code was taken out of `andor_scene.py`: `wheel_solid`, the wheel / flange / pitch materials, `--wheel-finish` and the `physical` mode.
- **Strip layers stay separate objects:** bezel, sensor surface, illumination (emissive, keyed from the GUI's own lit pixels) and labels.
- **GUI sync, checked at 100 % on every master:**
  - shortened cards with the four keys on the display glass;
  - FILTER;
  - the routing key in the header;
  - ARP + SUB with the SETTINGS and INFO keys;
  - no footer status line.

## 2. Masters

| File | Setup / shot | Size | Samples (noise threshold) |
|---|---|---|---|
| `C3_A1_R1_desktop` | GALLERY · A1_R1 | 3840 × 2160 | 128 (0.006) |
| `C3_A1_R1_W_desktop` | GALLERY · A1_R1_W: instrument about 6 % further right (shift −0.17, distance 1.87) | 3840 × 2160 | 128 (0.006) |
| `C3_A1M_R1_mobile` | GALLERY · A1M_R1 (19.5:9; crops to 9:16 top and bottom) | 2160 × 4674 | 128 (0.006) |
| `C3_A2_gallery` | GALLERY · A2 | 3600 × 2400 | 160 (0.005) |
| `C3_B1_R4_feed` | FLOATING · B1_R4 (dark 4:5) | 2160 × 2700 | 128 (0.006) |
| `C3_B2_115_R4_vertical` | FLOATING · B2_115_R4 (9:16) | 2160 × 3840 | 128 (0.006) |
| `C3_M01`–`M06` `_sharp` / `_cine` | MATERIAL · f/11 or f/5.6 | 2400 × 2400 (M01), 3000 × 2000 | 80 (0.008) |

**B1:**
- **What was tried:** besides R4, the brief allowed one more key-light test (R5).
- **Result:** R5 was not clearly better, so **R4 is kept**, as instructed.

**Details:**

| Shot | Subject |
|---|---|
| M01 | macros |
| M02 | source controls |
| M03 | display |
| M04 | keyboard |
| M05 | front-left corner: timber cheek, ceramic, PITCH bezel |
| M06 | touch strips |

## 3. Transparent set (A2 camera, 4200 × 2800)

| File | What it is | Samples |
|---|---|---|
| `C3_transparent_product` | Product only, RGBA. The floor is invisible to the camera | 96 |
| `C3_transparent_product_shadow` | Product plus the floor as a shadow catcher, RGBA | 96 |
| `C3_transparent_shadow_only` | The shadow catcher alone, RGBA, black RGB. Under the product's footprint the floor is fully occluded, so alpha is near 1 there; the product pass covers that area exactly | 64 |
| `C3_background_clean` | The stone floor and wall without the product, RGB | 64 |
| `C3_combined` | The beauty pass at the same camera | 96 |

**Checks:**
- **On three backgrounds** (`alpha_check_*.jpg`: light 247/246/242, mid 150/146/140, dark 24/22/20): clean edges, no fringe or halo around the timber, ivory or keys at native resolution.
- **Shadow edges agree:** at the same pixel, alpha is 38 for product + shadow and 39 for shadow only.

## 4. Web derivatives

`scripts/web_derivatives.py`:
- **Formats:** AVIF (quality 62, speed 4) and WebP (quality 86, method 6).
- **Widths:** 640 / 960 / 1280 / 1920 / 2560 / 3840 plus the master's own width, never upscaled.
- **Alpha:** kept on the transparent masters.
- **Log:** 276 files, each with its dimensions, settings and size, in `renders/web_c3/derivatives.md` and `.json`.
- **Resumable:** the script now keeps files already written, and can be run for single masters to keep memory low.

**Typical sizes at 1920 px:**

| Master | AVIF | WebP |
|---|---|---|
| A1-R1 | 42 KB | 70 KB |
| A1M-R1 | 73 KB | 121 KB |
| A2 | 60 KB | 100 KB |
| B1-R4 | 70 KB | 161 KB |
| B2 | 70 KB | 165 KB |
| Transparent product | 73 KB | 121 KB |
| Details | 10–35 KB | 25–62 KB |

**Recommendation:** serve AVIF with a WebP fallback.

## 5. Inspected at native resolution

| Master | What was checked |
|---|---|
| A1-R1, A1-R1 W | Header text, preset bar, card labels and values, waveforms, display keys, FILTER, ARP/SUB, SETTINGS/INFO, strips with their PITCH/MOD labels: all sharp |
| A1M-R1 | Product at 27–47 % of the height, under the headline zone |
| A2, B1-R4, B2 | The compositions as approved; nothing reopened |
| Transparent set | As in section 3 |
| Details | M01–M05 as intended |

**One observation for review, M06 (touch strips):**
- **What it shows:** at this close, shallow viewing angle, the satin sensor reflects the soft key across its whole face. So the lit PITCH segment reads as pale salmon rather than the GUI's orange.
- **Why it isn't fixed here:**
  - The approved surface (strip B) is correct, and it reads correctly in every full-product master.
  - Changing the material would reopen strip B.
- **Proposed fix, if wanted:** a detail-only light change, rotating the MATERIAL key about 15° so its reflection leaves the sensor. It needs two re-renders, about 25 minutes.

## 6. Reproduce

The final batch is `final.sh` (resumable: it skips existing masters). It runs `andor_scene.py` with the setup, shot and pass above, then:

```
python3 product-renders/blender/scripts/web_derivatives.py <masters dir> <out dir> [master …]
```
