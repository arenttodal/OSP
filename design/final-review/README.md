# Final visual review

The live editor rendered at the canonical size (1448 x 1086) by the hidden `[canonical]`
plugin test, compared with the two approved references (`design/reference/`). Regenerate
with `scripts/visual-review.sh` (set `OSP_CANONICAL_A` / `OSP_CANONICAL_B` to the
reference's own recordings, `MMZT_one_shot_sailboat_C.wav` and
`MMZT_one_shot_reverb_kalimba_C.wav`; without them stand-in sounds are rendered).

| Reference | Folder | Mean difference (0-255) |
|---|---|---|
| `main-2-layer.png` | `main-2-layer/` | 17.6 (from 33.7 before the pass) |
| `popup-space.png` | `popup-space/` | 20.9 (from 49.0) |

Each folder has `reference.png`, `implementation.png`, `overlay-50.png` (half each: an
edge that doubles is out of place) and `diff.png` (amplified difference).
`matrix-1.png` / `matrix-2.png` show the whole screenshot matrix (`01-empty` ...
`21-advanced-popup`) at one third size.

## Matching

- Geometry is laid out in the reference's own pixels (`apps/plugin/Source/Design.h`) and
  the whole instrument scales uniformly: housing, header, cards, wells, knob rows, the
  modifier buttons, the blend band, macros, envelope, wheels and footer sit within 1-2 px.
- Knobs, buttons, badges, LEDs, the A/B blend and the per-layer Reimagined thumbs, the
  waveform family (A warm amber, B cool mineral), the SPACE popup's shell, tabs, display
  and DECAY cell follow the reference's measured colours and proportions.

## Known differences (deliberate)

- **Waveform content** comes from the actual recordings and the live voices, so the
  peaks, read heads and grains are never pixel-identical to the painted mockup.
- **Keyboard**: the mockup's octaves are not uniform (C1-C7 about 173 px apart, the last
  octave squeezed to 150 px). The instrument draws a uniform 88-key keyboard in the same
  frame, so black keys drift up to about 10 px against the mockup towards the right.
- **VOLUME pointer**: the reference points at 12 o'clock; unity gain (0 dB on the
  -36..+12 dB range) sits at 75 % of the travel. The parameter's mapping is unchanged so
  saved host automation keeps its meaning.
- **SPACE DECAY 3.2 s with ROOM**: ROOM's decay range ends at 2.5 s (the DSP's design), so
  the canonical render shows 2.5 s and a 0-3 s axis where the mockup shows 0-4 s.
- **Popups other than SPACE** have no approved reference; they use SPACE's shell, tab bar,
  display and knob cells, and the earlier mockups' art direction
  (`design/reference/earlier/earlier-popups.png`).
