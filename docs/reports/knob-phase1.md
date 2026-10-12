# Rendered macro knob, phase 1

One photorealistic macro knob made by a reusable procedural pipeline, piloted on LIFE. The
images are in `knob-phase1/`. The generator is in `tools/blender-assets/` (see its README).
The renderer is `apps/plugin/Source/SpriteKnobRenderer`.

## 1. The existing knob (findings before any change)

**Drawing:**
- Every knob is drawn as vectors by `design::draw::knob` (`Design.cpp`).
- The macros pass `OspLookAndFeel::drawRotarySlider` properties: `arc`, `thinArc` (macro
  identity ring, 2.4 px), and `paramId` (modulation drop target).

**Size:**
- Each macro slider is a 132 × 132 box on the 1448 × 1086 design canvas. With ticks, its
  body radius is 132 / 2 / 1.42 = **46.5 px (93 px diameter)**.
- The canvas is scaled as a whole (`content.setTransform`): the default window is 1086 px
  wide (scale 0.75), the range is 869–1810 px, and `setScaleFactor` adds the host scale.
- So the body is **70 px at 1×** and **140 px on Retina** at the default size, and up to
  232 px on Retina at the largest window.

**Mapping:**
- `setRotaryParameters (1.25 π, 2.75 π)`: a 270° turn, minimum at 7:30 and maximum at 4:30,
  clockwise.
- The knob draws `start + sliderPos × (end − start)`, where sliderPos comes from the slider
  (the parameter's own range and skew).

**Halos:** `ModulationOverlay` draws them separately around the slider's centre, at its own
radius. The ticks and the value arc are drawn before the body.

**Palette:**
- Macro panel ≈ (231, 224, 211).
- Knob cap `#f8f3ea` → `#d9d0c2`, rim `#b9ad9b`, pointer `#141210`.

**Not available:** no Eclyde / UI Mother assets are in the repository or this environment.
The reference images were the user's two knob photographs; direction A (shallow, tapered,
fluted) was preferred.

**Checkpoint:** local tag `knob-phase1-baseline` (= `111f466`).

## 2. The generator

**How it runs:**
- Blender 4.2 Cycles, headless: `bpy` as a Python module here, or `blender -b -P`.
- Every value is in `config/macro-knob.json` plus named presets: `materials/ivory.json`,
  `lighting/studio-soft.json`, `scenes/cameras.json`.
- Stages: calibrate, variants, indicators, cameras, frames.
- `postprocess.py` (Pillow) makes the sheets, packs, atlas and manifest. `verify.py` checks
  the masters.

**Geometry** (mm):
- 28 mm diameter, 11 mm high, 7 % taper, 1.5 mm soft bevel, 0.55 mm dome.
- 48 rounded flutes, 0.42 mm deep, fading in above the base and out across the bevel.
- A 1.4 mm base ring, 0.45 mm wider, behind a 0.35 mm groove.
- Every corner is filleted.

**Studio:**
- Light: a 140 mm soft key at the upper left (70° high), a fill at the lower right, a faint
  edge light and ambient. The rig is fixed; only the knob turns.
- Shadow: a shadow-catcher panel, so only the contact shadow is in the alpha.
- Camera: orthographic, straight down. 6° and 12° tilts were tested (`cameras.png`): richer
  sidewalls, but the cap's centre leaves the halo's pivot and the outline is no longer a
  circle. Rejected for the pilot.

**Exposure:** solved so the top face lands on `#f1ede6`, beside the panel.

**Render settings:** 512 px masters, 96 samples, OIDN, fixed seed, no adaptive sampling,
Standard view transform.

**Time:** about 40 s per frame on this 4-core VM; the 129 frames took 1 h 47 min.

**Determinism:** frame 64 rendered twice is bit-identical. `verify.py` passed:
- 129 frames, all 512 px;
- the border is clear;
- the largest indicator error is 0.24°.

## 3. Material variants and indicator (`variants.png`, `indicators.png`)

| Variant | What it is | At real size |
|---|---|---|
| A, Soft Ivory | roughness 0.56, a little sheen, powder grain | calm, the flutes soft |
| B, Brushed Ivory | circular brushed grain | the brushing is invisible at 70 / 140 px: same as A |
| **C, Satin Ivory** | roughness 0.36, a thin soft coat | **a crisp, soft rim highlight; reads as the most "made" object at both sizes** |

The microtexture is invisible even on the 512 px masters, which is what the brief asked
for: the richness comes from geometry and light.

**Indicator:**
- Engraved with a matte charcoal inlay against a printed line: the engraved one stays
  darker and crisper at 70 px.
- Widened from 1.05 to 1.3 mm for 1×. It runs from 30 % of the radius to the edge and down
  the sidewall, as on the reference cap.

**Chosen:** Satin Ivory, engraved, 1.3 mm.

## 4. The frames and runtime resources

**Frames:**
- 129 frames: frame 0 = 1.25 π (minimum), frame 128 = 2.75 π (maximum), 2.1° apart.
- 257 is a config change (`--set frames.count=257`). It was not needed: at the default size
  the indicator moves about 2 px per frame on Retina, and the slider moves about 1° per
  pixel dragged.

**Packs:**
- Two levels, frame side 176 and 352 px (body 121 / 243 px): `macro-ivory-176.zip`
  (2.6 MB) and `macro-ivory-352.zip` (8.4 MB) in `apps/plugin/Resources/knobs/macro-ivory/`.
- They are embedded with `juce_add_binary_data`, adding **11 MB to the plugin binary**.
- Each is a zip of PNGs plus `manifest.json` (schemaVersion 1). Stored, not deflated, with
  fixed timestamps, so the bytes are reproducible.
- The atlas and contact sheet are generated for review (`contact-sheet.png`) and are not
  shipped.

## 5. The renderer

**Data flow:**
- `SpriteKnobAsset` reads the packs and checks them: every frame present, levels agreeing,
  a plausible manifest.
- A frame is decoded the first time it is shown. It is resampled once, by exact area
  averaging in premultiplied ARGB, to the physical pixels it covers. That size is the
  window scale × the display scale (`getPhysicalPixelScaleFactor`). The frame is then drawn
  1:1, with no resampling while painting.
- Stored frames are kept only when they are the drawn size. Exact-size sets are kept for
  the 3 most recent sizes, so a live resize does not collect sets.

**Sharing:** one `SpriteKnobLibrary` per process (`SharedResourcePointer` in the
look-and-feel), shared by every editor of every instance.

**Integration:**
- `design::draw::knob` draws the ticks, track, value arc and macro ring as before. With
  `KnobStyle::sprite` set, it draws the rendered cap in place of the drawn body and pointer,
  at the same radius.
- Halos, popovers, value bubbles, context menus, dragging, double-click and accessibility
  are the slider's own and are untouched.

**Fallback:** a pack that does not read leaves the drawn knob.

**Development switch** (not saved; never a parameter, state or anything the audio thread
reads):
- `Cmd+Option+K` cycles original → pilot (LIFE) → all six macros → original.
- `OSP_KNOB_SKIN=original|pilot|macros` sets it at start-up.
- The default is **pilot**.

## 6. Verification (`[plugin][sprite-knob]`, 9 cases, 2053 assertions, all pass)

1. Every frame of both sizes decodes at its size, transparent around and solid in the
   middle. The pack's angles equal `OspLookAndFeel::rotaryStart` / `rotaryEnd`.
2. Frame order: minimum, the quarter points and maximum map to frames 0, 32, 64, 96 and 128.
   The indicator read from the pixels of every frame agrees within 3°: lower left at 0,
   straight up at 64, lower right at 128.
3. Pivot and size: at window × display scales 0.6–2.5, the solid cap's centroid is within
   0.75 px of the slider's centre, at 90–100 % of the body radius.
4. Caches: a repaint makes nothing new; a new size makes one frame; a resize keeps at most 3
   sizes.
5. Area resampling: flat colour stays exact, a checkerboard averages to grey, and a half-
   transparent edge keeps its colour (no dark fringe).
6. A damaged pack is refused with a reason, and the drawn knob is shown.
7. The skin changes looks only:
   - the instrument rendered with the editor open and the skin cycling is **bit-identical**
     to one without an editor;
   - the saved state is byte-identical and LIFE's value unchanged;
   - the rendered frame follows host automation at 0 / 25 / 50 / 75 / 100 %;
   - the pilot skin renders LIFE only; the macros skin renders all six.
8. Halos with one route, two routes and meta-modulation have the same centre and radius
   under both skins. The rendered cap is centred on the halo within 1 px.
9. Two instances share one asset: the second editor decodes nothing new.

**Screenshots** (headless, the real editor):
- `full-original-1086.png` / `full-pilot-1086.png`: the whole plugin at the default size, 1×.
- `macros-original-vs-rendered-2x.png`: the macro row on Retina, drawn vs all six rendered.
- `closeups-2x.png`: LIFE at 0 / 25 / 50 / 75 / 100 % and with one route, several routes and
  meta-modulation (top: drawn, bottom: rendered).

**Not tested here:** the plugin in a DAW on the Mac, real Retina displays and macOS'
CoreGraphics renderer. The headless snapshots use JUCE's software renderer at 1× and 2×.
These are on the Mac checklist.

## 7. Performance (`[.][knob-perf]`, this VM; medians)

| | Drawn knob | Rendered, first show of a frame | Rendered, after |
|---|---|---|---|
| LIFE paint, 1× (design size) | 0.64 ms | 1.28 ms (decode + resample) | **0.33 ms** |
| LIFE paint, 2× | 2.56 ms | 4.19 ms | **0.26 ms** |

**The whole editor at 2×:**
- original 200 ms, pilot 225 ms, all macros 202 ms.
- Noise on a busy VM dominates these numbers; no measurable difference.

**Memory, held:**
- Pilot at 2×: 159 KB (one frame).
- All six macros: 796 KB.
- Worst case, every one of 129 frames visited: 9 MB at 1× and 37 MB at 2× of the design
  size. At the default window on Retina the worst case is about 21 MB.

**Audio thread:** no work and no state. The bit-identical render with the skin cycling
proves it.

## 8. Recommendation

**Use the custom Blender knob** for the macros. After first show it paints faster than the
drawn knob. It holds under a megabyte in normal use, and it changes nothing in the sound or
the state. At the real sizes it reads as a physical, made object: the fluted rim, the soft
rim highlight and the engraved line, in the panel's own ivory. The drawn knob reads as a
flat disc.

**The Eclyde knob:** not available here, so it could not be compared.

**Before rolling it out:**
1. Judge it on your Mac at the default size, with `Cmd+Option+K` to flip between them.
   CoreGraphics' interpolation differs from the headless renderer.
2. If the binary size matters, drop the 352 px level to 288 px (about −3 MB, sharp up to
   the default window on Retina) or encode the frames as indexed-alpha PNGs.
3. For memory-sensitive hosts, cap the held frames (an LRU of about 32). It is a few lines
   in `SpriteKnobAsset::frameAtSide`.
4. Then the engine-card knobs, from `config/future/medium-engine-knob.json`.
