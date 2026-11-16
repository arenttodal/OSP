# Blender-to-GUI asset upgrade · gate 1: the six macro knobs (16 Nov 2026)

This pilot covers the six macro knobs only. The knob's proportions, the GUI layout, the value arcs,
the halos, the LEDs, the control behaviour and the DSP are all unchanged. The arcs and halos stay
vector-drawn; nothing is baked into the sprites. The manifest format, the packaging and the vector
fallback are unchanged too.

## Candidates

| | What | Result |
|---|---|---|
| **A** | The current production sprites (`studio-soft`) | Flat. The flutes read as a printed texture, and the knob barely lifts off the panel |
| **B** | The marketing model's G knob (0.30 D, 8.4 mm, flutes 0.252) in the GUI rig, `studio-sculpt` | Better flutes, but lower than the GUI's knob: it reads smaller and loses the cap's shoulder |
| **C** | The production height (11 mm) and material in the new `studio-sculpt` rig (key az 128° el 58°, size 100, 0.30; fill 0.10; edge 0.05; world 0.12) | **Chosen.** Defined flutes, a soft shoulder, a contact shadow toward the lower right, and the same footprint |

Calibration against the panel ivory (#f1ede6): B +0.277 EV, C +0.271 EV, so the cap's mid-tone
matches A's.

## Implemented

- `tools/blender-assets/config/macro-knob.json`: `"lighting": "studio-sculpt"`, the only config
  change.
- `shared/andor-ui/Resources/knobs/macro-ivory/macro-ivory-{176,352}.zip`: rebuilt from a complete
  129-frame turn by `postprocess.py --stage frames`.
- `verify.py`: 129 frames at 512 px; the largest indicator error is 0.21°.

## Tests (build-plugin)

- `[sprite-knob]`: passed (2053 assertions).
- `[knob-shots]`: passed.
- `[design-capture]`: passed.
- `[knob-perf]`: see below.

Hidden tests were run with `OSP_SNAPSHOT_DIR` / `OSP_DESIGN_CAPTURE` set.

## Real-editor comparisons (`knob-pilot-gate1/`)

- `04-window-{869,1086,1810}_macros.jpg`: the macro row at 1:1 in each window size, A above C.
- `03-three-layers@{1.5x,2.0x}_macros.jpg`: HiDPI and Retina.
- `life_min_default_max_2x.jpg`: LIFE at min, default, max, 25 % and 75 %, at 2×, enlarged 2×. The
  pointer sits exactly at the arc's end at every value.
- `life_mod_{one,several,meta}.jpg`: modulation arcs and halos, still vector, over the new sprite.
- `compare_{176,352}.jpg`: the A / B / C frame comparison at both pack levels.
- `candidate-C-turn.png`: a contact sheet of the full turn.

## Performance

The frame size (176 / 352 px), the frame count (129) and the decode path are unchanged, so the
decoded memory and the paint cost are the same as A. Only the zip size changes.

| | A | C |
|---|---|---|
| 176 pack | 2.69 MB | 2.70 MB |
| 352 pack | 8.60 MB | 8.88 MB |

`[knob-perf]` for LIFE, C:

| Scale | Drawn (median / p95) | First show | Then (median) | All frames held |
|---|---|---|---|---|
| 1× | 0.58 / 0.72 ms | 1.35 ms | 0.26 ms | 9.2 MB |
| 2× | 2.44 / 3.84 ms | 3.94 ms | 0.23 ms | 36.7 MB |

Whole editor at 2×, all macros skinned: 111 ms median. That is the same as the original skin
(111 ms), so the change is within noise.

## 129 vs 257 frames

At 129 frames over the 270° travel, each step is 2.1°. The pointer tip moves 0.95–2.0 px per step
at 1× and 1.9–4.0 px at 2×.

Every level must share one frame count. 257 frames would double both packs, to about 5.4 and
17.8 MB, and the held memory with them.

**Recommendation:** keep 129. Judge 257 only if stepping is visible in a slow drag on a Retina Mac;
the stills show none.

## Not in this gate

The small knobs, buttons, ceramic and keyboard come after approval of this pilot.
