# Gate C · Checkpoint C2.5: final creative refinement (17 Nov 2026)

These are drafts for review: 0.4–0.6 scale, 24–64 samples. The final full-resolution batch starts only after approval. The C2
shots and scenes are unchanged and still render as before. Every C2.5 change is either a new shot in
`blender/config/campaign_setups.json` (shot `overrides`, so setups stay as approved) or an opt-in switch in `andor_scene.py`.

## 0. The marketing model now matches the current editor

| | Before (the model's 9 Nov capture) | Now (OSP default branch `08ebd01`, built and captured 17 Nov) |
|---|---|---|
| Source cards (3 layers) | A four-key row under the knobs | The cards are 64 px shorter (`EngineCard::footTrim`). The four keys sit as small icons on the display glass |
| Header | Preset bar, save | Plus the routing key |
| Bottom right | ARP pill, `Advanced >` | An ARP + SUB block, plus SETTINGS and INFO keys |
| Macro names | CHARACTER | FILTER |
| Bottom edge | Status line (`Ready · 5 notes`) | None. The editor's compact edge is unchanged in the marketing model: its approved physical front return keeps the same relation to the keys |

**How it was done:**
- **Capture:** a local, uncommitted capture test in a detached worktree of the default branch exported the hero (three layers, chord held) with and without caps, with every knob's geometry and every component's bounds.
- **`prep_textures.py`:** the layout under the cards now comes from those bounds. Run on the old inputs, it reproduces the committed geometry exactly.
- **`andor_scene.py`:** `KY` carries the 64 px to every editor y below the cards. It is 0 for older captures, so C2 rebuilds unchanged.
- **Output:** the new textures are in `blender/textures_current/`; the C2 textures in `blender/textures/` are kept.
- **Chassis:** the instrument is 64 editor px (17.7 mm) shorter from back to front, because the cards are. Nothing else on the chassis changed.

**Fixed while syncing:**
- The macro LED lenses were hard-coded at the old y. In the synced model they would have floated over the keys.
- The macro and card camera presets, and any shot target under the cards, now follow `KY`.

## 1. A1 desktop hero

| | Change |
|---|---|
| **R1 Warm Gallery** | Stone 0.585/0.556/0.512 → 0.655/0.622/0.573: about 12 % brighter, same hue ratios. Key 1.4 × 1.0 → 1.1 × 0.8 m (more form), Overhead 3 → 2.2 W, Fill 2 → 1.6 W. Detail rake: 0.9 × 0.12 m from behind left, 0.8 W (about 12 % of the key). Cheek glint: a narrow 2.5 W sheen along the left teak cheek |
| **R2 Architectural Ivory** | Stone 0.735/0.708/0.662, world 0.09. Key 0.9 × 0.6 m at 32 W (stronger directional shadows), Overhead 1.6, Fill 1.2. Same rake and glint |

- **First attempt, rejected:** the Detail rake at 3 W washed the product out (it added about half the key's light to the top surface) and was cut to 0.8 W.
- **Result:** R1 models the flutes, small-knob bevels and card edges best without lifting the ivory. **Recommended: R1.**
- **Website preview (`02`):** live type over the clean master. The headline is sized to the free space left of the product (29 % of the width), about 1.3× C2's preview.
  - It carries the nav (Instrument, Sounds, Questions), CTA (Join the waitlist / Hear it) and the existing line "One recording is enough to begin." (all from the site).
  - A larger headline would need the product about 6 % further right (A1 camera shift −0.13 → −0.19). Offered as an option, not done.

## 2. A1M mobile hero

- **Master:** a phone-shaped (19.5:9, 1170 × 2532) product-only master, A1M-R1, with R1's light and stone.
- **Position:** the product spans 27–47 % of the height, right under the headline zone (8.5–23.5 %). In C2 it sat at 42–69 % of a 9:16 frame.
- **Frame:** the product still fills about 88 % of the width.
- **Layout guides:** nav 0–6.5 %, CTA 56–63 % (in the config, for Framer).
- **Previews:** at 390 × 844 and 430 × 932 CSS (3×, cover-fit). On 9:16 screens the master crops top and bottom floor only.
- **Camera shift:** with horizontal sensor fit it is measured against the frame's width. Noted in the config.

## 3. A2 product photography

Camera, scale, environment and materials are unchanged; the model is synced. The draft shows the current interface (trimmed cards, display keys, FILTER, SETTINGS / INFO). **The final 3600 × 2400 master is re-rendered after approval.**

## 4. B1 dark flagship: the light ratio

| | Fill | Top line | Key size | Mean change vs R0 |
|---|---|---|---|---|
| R0 (C2) | 0.8 W | 4.5 W | 0.9 × 0.5 m | — |
| R1 | −15 % | — | — | 0.02 / 255 |
| R2 | −25 % | — | — | 0.04 / 255 |
| R3 | −25 % | −25 % | — | 0.45 / 255 |
| **R4** | −25 % | −25 % | 0.6 × 0.35 m | 0.46 / 255; the crispest sidewalls and contact shadows |

- **Why R1 and R2 do nothing:** the Fill light is 0.8 W against about 60 W of product light, so cutting it changes nothing. The flat white-on-white comes from the broad Top line and the large soft key.
- **R4, recommended:** the floor lights are untouched.
- **A further step:** the next step for more definition is moving the key, not cutting the fill further.

## 5. B2 vertical (+15 %, locked)

`B2_115_R4`: the locked composition with B1-R4's light; product-only and type preview. Copy at 14–34 %, CTA in the bottom 20 %. Current platform safe zones should be checked before the final exports.

## 6. PITCH/MOD

**Mechanical wheel, rejected:** the C2 physical-wheel interpretation was wrong for ANDOR. Its controls are touch strips.

**Touch strips (`--wheels strip`):**
- **Construction:**
  - one continuous, full-height sensor filling the bezel's whole opening;
  - rounded in plan;
  - a convex crown across its width along its whole length (`--strip-crown`: A 0, **B 0.02**, C 0.04 of the width);
  - the crown's peak 0.06 mm under the bezel top;
  - satin charcoal: metallic 0, roughness 0.36–0.40 with micro variation, a thin satin coat.
- **Illumination:** a separate object that adds the GUI's own lit pixels (orange PITCH, blue MOD) over the transparent surface, so shape, width, gradient, rounded ends and value are the capture's.
- **Separable parts, as future GUI layers:**
  - bezel (static);
  - surface (static);
  - illumination (dynamic);
  - labels and the MOD ring (artwork).
- **Comparisons:** see `06_touch_strips` and `06b_touch_strips_check` (renders 1–8). **Not merged into the master until approved.**
- **Lit-shape mask:** keyed on hue, (R − B) / max(R, B), not brightness. So the whole orange gradient, light at the top and deep at the bottom, keeps its full width. Half an 8-bit step of noise stops the capture's gradient from banding once emitted.
- **Review cameras:** `strips` (no depth of field), `strips34` (sharp three-quarter) and `strips_top` (orthographic, product unrotated, editor x 11–161). The near clip plane is now 5 mm, so macro cameras never cut into the body.

**Timber junction and slots, root causes fixed (not hidden):**
1. **The dark sliver** at the left cheek was the C2 slot-material rule catching the panel's outer side wall. Charcoal is now only inside the slots.
2. **The light floor seen in the C2 slots** came from the slot stopping on the housing tray 1.2 mm under the panel skin. The slot now continues into the tray, with charcoal walls.
3. **A black wedge at the front-left corner:** a tray-top polygon that took the slot material. Now only faces below the tray top do.

## 7. Detail library previews

**M02 (source controls) and M04 (keyboard):** the MATERIAL setup's light at f/11.
- **M02:** shows the waveform glass and the small knobs' bevels.
- **M04:** shows the white-key fronts, the black-key height, the ivory return and the shadow separation.
- **Still to come:** M01 = C1 / C1S (approved), M03 (display), M05 (timber and ceramic) and M06 (touch strips, after approval).

## 8. Export plan (prepared, not started)

| Asset | Master | Notes |
|---|---|---|
| Desktop hero A1 | 3840 × 2160 | R1 (or R2) once chosen |
| Mobile hero A1M | 2160 × 4680 (19.5:9) | Crops to 9:16 (2160 × 3840) top and bottom |
| Gallery A2 | 3600 × 2400 or higher | Re-render on the synced model |
| Dark feed B1 | 2160 × 2700 | R4 light |
| Vertical B2 | 2160 × 3840 | +15 %, R4 light |
| Details | 2400–3200 px long edge | M01–M06 |
| Transparent product | ≥ 4000 px | Clean product; product + shadow catcher; shadow-only; background; combined. Checked on light, mid and dark backgrounds |

- **Formats:** web derivatives as AVIF and WebP at 640 / 960 / 1280 / 1920 / 2560 px widths from the PNG masters. Dimensions, quality settings and sizes are logged per file.
- **Typography:** stays live in Framer.
