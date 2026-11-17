# GUI assets · Gate 2 (source knobs) and Gate 3 (mini knobs, first studies) · 17 Nov 2026

Measured in the current OSP editor (default branch), built locally with review-only wiring that is not committed.
Sheets are in `gui-assets-gate2-3/`.

## D2 macro status

**PR:** [arenttodal/OSP#26](https://github.com/arenttodal/OSP/pull/26), open from `claude/gui-macro-knob-d2` (`3354690`) onto the default branch (`56bab47`).
- **Scope:** only the two `macro-ivory` packs, the shadow trim in `postprocess.py`, the macro config and the `studio-sculpt` preset.
- **macOS CI:** green.
- **Still to do:** the Retina Mac and DAW interaction checks are manual and must happen before shipping.

## Gate 2 · `layer-ivory` (START, TUNE, PAN, LEVEL, REIMAGINED)

**Current knob:** body radius 26.1 reference px, in the five-knob row of each card. It is a domed cap on a fine skirt with a short, heavy pointer near the rim. The layer colour is a vector arc; REIMAGINED's arc is spectral.

The new asset has its own geometry (`tools/blender-assets/config/layer-knob.json`); it is not a scaled-down macro.

| | Geometry | Result at the real size |
|---|---|---|
| A | The current vector knob | Clean but flat |
| **B (recommended)** | 20 mm cap, 7 mm tall (H/D 0.35, below the macro's 0.39), domed top, fine skirt, **smooth sidewall**, 1 mm bevel. Pointer from 0.52 to 0.92 of the radius (the current knob's) | Reads cleanest at 1× (76 px frame): a soft shoulder and a short contact shadow, with no surface noise |
| C | B with 32 fine flutes, a 1.4 mm bevel and 7.6 mm height | Handsome at 2×, but at 1× the flutes break into a jagged rim |

- **Shadow:** B's cast shadow is trimmed to 55 % (D2: 80 %), with a 0.3 mm contact band and a 0.8 mm ramp. The cap's own pixels are untouched.
- **Light:** the D2 light (`studio-sculpt`), calibrated to the same panel target, so the cap's mid-tone matches the macros.

**Runtime levels: 100 and 200 px**, not the macro's 176 / 352.
- The body is 52 px at 1448.
- The largest physical need is a 1810 px window at 2×: 131 px body, 196 px frame including the 1.45 margin.

**In the real editor** (`layer_editor_card_2x.jpg`, `_macros_2x.jpg`, `_full_1x.jpg`):
- B and the D2 macros are drawn by the unchanged `SpriteKnobRenderer`. Only the `"sprite"` property was set on the source dials.
- The pointers meet the arc ends exactly at START 0 %, LEVEL 0.0 dB and REIMAGINED 24 %. The ticks, arcs, halos and REIMAGINED's spectral arc are vector, as before.
- The review pack holds exact frames for the values shown, plus the five representative positions. The full 129-frame turn is rendered after approval.

## Gate 3 · `mini-ivory` and `mini-graphite` (exploratory)

**One geometry for both:**
- 12 mm cap, 4.8 mm tall, a shallow dome;
- smooth sidewall, no skirt, a 0.6 mm bevel;
- a wide indicator (from 0.30 to 0.92 of the radius);
- cast shadow at 50 %.

**Graphite:**
- the same geometry in a new `satin-graphite` finish (`materials/graphite.json`) with a light indicator;
- calibrated to its own graphite target. Calibrating it to the ivory panel target pushed it near white; that is fixed.

**Tested at the real body sizes**, 1× and 2× (`mini_ivory_actual_sizes.jpg`, `mini_graphite_actual_sizes.jpg`):
- about 12 px: the popover's second row in the smallest 869 px window;
- 19 px and 26 px: popover cells;
- 40 px: envelope, mod panel, ARP;
- 49 px: rack cells.

**What the studies show:**
- The ivory mini keeps a clear silhouette and pointer even at about 12 px.
- The graphite mini's pointer stays readable, but its silhouette is faint against the dark display at the smallest size. It needs a slightly lighter rim or a touch more key on the edge before it ships.

**Proposed levels: 48, 96 and 180 px.** None of the roughly 150 call sites are changed.

## Memory (rendered families visible together)

**Test:** three layers loaded, then every rotary in the editor swept at 2× (33 of the 129 positions) in 1086, 1448 and 1810 px windows.

| Skin | Macro asset held | Layer asset held | Process RSS |
|---|---|---|---|
| Vector (original) | — | — | +2.4 MB |
| Rendered (D2 macros + `layer-ivory`) | 9.2 MB | 4.9 MB | +23.4 MB |

- **Projected worst case** (all 129 frames at all three sizes): about 36 MB macro and 19 MB layer.
- **Why it stays shared:** each asset is one shared object per process (`SpriteKnobLibrary`), however many knobs or instances show it. Frames decode lazily and exact-size frames are cached per size.
- **Policy for the minis:** if the cache needs a ceiling, an LRU per asset like the ceramic skin's is the place for it. It isn't needed for macro + layer.

## Snapshot / resize memory retention: not the sprites

**Test:** the same resize sweep (869 → 1810 px, three rounds, painted at 2× into one reused image).

| Skin | Process memory | Sprite caches |
|---|---|---|
| Vector | +283 to +419 MB | 0 |
| Rendered | +295 MB | 6.3 MB |

- **Cause:** the growth is the ceramic skin's resampled-piece cache, an LRU capped at **192 MB / 160 images** (`CeramicSkin.cpp`) and shared per process, plus allocator retention. It is bounded by design, not a leak, and it predates the sprites.
- **Suggested separate ticket:** lower the cap, or drop the other sizes' pieces when a resize ends.
