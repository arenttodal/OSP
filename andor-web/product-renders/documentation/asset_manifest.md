# ANDOR commercial stills · final asset manifest

Repository `arenttodal/OSP`, branch `claude/friendly-pascal-0y48r0`. Blender 4.2 (bpy), Cycles CPU.

**Sources:** `scripts/andor_scene.py` @ `e50aaaf`, `config/campaign_setups.json` @ `a233b26`, `scripts/prep_textures.py` @ `1a0cf7e`, `scripts/web_derivatives.py` @ `2399528`.
**Scene textures** (`blender/textures_current/`, git-ignored, archived with Package A): `geometry.json` sha256 `f13d38eaa767…`, `osp_face.png` sha256 `e2a8abc7fbd5…`, `teak_strip.png` sha256 `cb7ecafcb641…`.

**Revision, 21 Nov (interface audit, `review_fixes_21nov.md`):**
- **M01 sharp and cinematic:** re-rendered on the frozen interface (`textures_0.9.0-beta.1`, 32e0bee), because the macro arcs had changed colour.
- **M06 sharp:** the approved R2 master, with only its top-edge arc replaced from a frozen-interface render. This affects 1.2 % of the frame; every other pixel is unchanged.
- **M06 cinematic and all other masters:** as listed below, on the 17 Nov interface. The visible difference is at most a footer key, 0.04 % of the frame.

Every master is a 16-bit PNG in sRGB (AgX view, 'AgX - Punchy' look, baked). The derivatives are 8-bit sRGB AVIF and WebP. The full camera, light overrides, command line and SHA-256 hash of each master are in `asset_manifest.json`.

| Asset | Master | Size | Alpha | Setup · shot · pass | Camera | Samples | Use | Derivatives |
|---|---|---|---|---|---|---|---|---|
| A1M_R1_mobile | `C3_A1M_R1_mobile_2160x4674.png` | 2160 × 4674 | no | ANDOR_GALLERY · A1M_R1 · beauty | yaw -30, pitch 41, 85 mm, f/11 | 128 | Website mobile hero (19.5:9; crops to 9:16 top and bottom) | AVIF + WebP at 640, 960, 1280, 1920, 2160 |
| A1_R1_W_desktop | `C3_A1_R1_W_desktop_3840x2160.png` | 3840 × 2160 | no | ANDOR_GALLERY · A1_R1_W · beauty | yaw -30, pitch 52, 85 mm, f/11 | 128 | Website desktop hero, wide-headline layout (product about 6 % further right) | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840 |
| A1_R1_desktop | `C3_A1_R1_desktop_3840x2160.png` | 3840 × 2160 | no | ANDOR_GALLERY · A1_R1 · beauty | yaw -30, pitch 52, 85 mm, f/11 | 128 | Website desktop hero (live type left of the product) | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840 |
| A2_gallery | `C3_A2_gallery_3600x2400.png` | 3600 × 2400 | no | ANDOR_GALLERY · A2 · beauty | yaw -26, pitch 50, 85 mm, f/11 | 160 | Gallery / product photography | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3600 |
| B1_R4_feed | `C3_B1_R4_feed_2160x2700.png` | 2160 × 2700 | no | ANDOR_FLOATING · B1_R4 · beauty | yaw -36, pitch 58, 85 mm, f/9 | 128 | Dark campaign, 4:5 feed | AVIF + WebP at 640, 960, 1280, 1920, 2160 |
| B2_115_R4_vertical | `C3_B2_115_R4_vertical_2160x3840.png` | 2160 × 3840 | no | ANDOR_FLOATING · B2_115_R4 · beauty | yaw -36, pitch 58, 85 mm, f/9 | 128 | Dark campaign, 9:16 vertical (stories, reels) | AVIF + WebP at 640, 960, 1280, 1920, 2160 |
| M01_cine | `C3_M01_cine.png` | 2400 × 2400 | no | ANDOR_MATERIAL · M01_cine · beauty | yaw -18, pitch 60, 100 mm, f/8.0 | 80 | Detail: macros | AVIF + WebP at 640, 960, 1280, 1920, 2400 |
| M01_sharp | `C3_M01_sharp.png` | 2400 × 2400 | no | ANDOR_MATERIAL · M01_sharp · beauty | yaw -18, pitch 60, 100 mm, f/14.0 | 80 | Detail: macros | AVIF + WebP at 640, 960, 1280, 1920, 2400 |
| M02_cine | `C3_M02_cine.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M02_cine · beauty | yaw -20, pitch 57, 100 mm, f/5.6 | 80 | Detail: source controls | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M02_sharp | `C3_M02_sharp.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M02_sharp · beauty | yaw -20, pitch 57, 100 mm, f/11.0 | 80 | Detail: source controls | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M03_cine | `C3_M03_cine.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M03_cine · beauty | yaw -14, pitch 40, 100 mm, f/5.6 | 80 | Detail: display | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M03_sharp | `C3_M03_sharp.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M03_sharp · beauty | yaw -14, pitch 40, 100 mm, f/11.0 | 80 | Detail: display | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M04_cine | `C3_M04_cine.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M04_cine · beauty | yaw -30, pitch 63, 100 mm, f/5.6 | 80 | Detail: keyboard | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M04_sharp | `C3_M04_sharp.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M04_sharp · beauty | yaw -30, pitch 63, 100 mm, f/11.0 | 80 | Detail: keyboard | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M05_cine | `C3_M05_cine.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M05_cine · beauty | yaw -48, pitch 60, 100 mm, f/5.6 | 80 | Detail: front-left corner (timber, ceramic) | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M05_sharp | `C3_M05_sharp.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M05_sharp · beauty | yaw -48, pitch 60, 100 mm, f/11.0 | 80 | Detail: front-left corner (timber, ceramic) | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M06_cine | `C3_M06_cine.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M06_R2_cine · beauty | yaw -40, pitch 58, 100 mm, f/5.6 | 80 | Detail: PITCH / MOD touch strips | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| M06_sharp | `C3_M06_sharp.png` | 3000 × 2000 | no | ANDOR_MATERIAL · M06_R2_sharp · beauty | yaw -40, pitch 58, 100 mm, f/45.0 | 80 | Detail: PITCH / MOD touch strips | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3000 |
| background_clean | `C3_background_clean_4200x2800.png` | 4200 × 2800 | no | ANDOR_GALLERY · A2 · background | yaw -26, pitch 50, 85 mm, f/11 | 64 | Clean background plate without the product | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840, 4200 |
| combined | `C3_combined_4200x2800.png` | 4200 × 2800 | no | ANDOR_GALLERY · A2 · beauty | yaw -26, pitch 50, 85 mm, f/11 | 96 | Combined beauty pass at the transparent set's camera | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840, 4200 |
| transparent_product | `C3_transparent_product_4200x2800.png` | 4200 × 2800 | yes | None · None · beauty |  | None |  | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840, 4200 |
| transparent_product_shadow | `C3_transparent_product_shadow_4200x2800.png` | 4200 × 2800 | yes | None · None · beauty |  | None |  | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840, 4200 |
| transparent_shadow_only | `C3_transparent_shadow_only_4200x2800.png` | 4200 × 2800 | yes | ANDOR_GALLERY · A2 · shadow | yaw -26, pitch 50, 85 mm, f/11 | 64 | Shadow-only layer (black, alpha) to pair with the transparent product | AVIF + WebP at 640, 960, 1280, 1920, 2560, 3840, 4200 |
