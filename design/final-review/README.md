# Final visual review

The live editor rendered at the canonical size (1448 x 1086) by the hidden `[canonical]`
plugin test, compared with the two approved references (`design/reference/`). Regenerate
with `scripts/visual-review.sh` (set `OSP_CANONICAL_A` / `OSP_CANONICAL_B` to the
reference's own recordings, `MMZT_one_shot_sailboat_C.wav` and
`MMZT_one_shot_reverb_kalimba_C.wav`; without them stand-in sounds are rendered).

| Reference | Folder | Mean difference (0-255) |
|---|---|---|
| `main-2-layer.png` | `main-2-layer/` | 17.3 (from 33.7 before the pass) |
| `popup-space.png` | `popup-space/` | 39.4 - see below: the macro popups are compact popovers now |

Each folder has `reference.png`, `implementation.png`, `overlay-50.png` (half each: an
edge that doubles is out of place) and `diff.png` (amplified difference).
`matrix-1.png` / `matrix-2.png` show the whole screenshot matrix (`01-empty` ...
`22-cleared`) at one third size; `typography-crops.png` compares the reference, the build
before the typography revision and the revision for the header, source header, source
controls, macro row, ADSR, mix band and Advanced.

## Matching

- Geometry is laid out in the reference's own pixels (`apps/plugin/Source/Design.h`) and
  the whole instrument scales uniformly: housing, header, cards, wells, knob rows, the
  modifier buttons, the blend band, macros, envelope, wheels and footer sit within 1-2 px.
- Knobs, buttons, badges, LEDs, the A/B blend and the per-layer Reimagined thumbs, the
  waveform family (A warm amber, B cool mineral), the SPACE popup's shell, tabs, display
  and DECAY cell follow the reference's measured colours and proportions.

## Revisions after the first final review

- **Macro popups are compact popovers again** (the user's correction): one at a time,
  unfolding directly above the clicked macro (230-270 x 180-215 px), no close button and
  no backdrop. `popup-space.png` (a large centred panel) is therefore no longer the target
  for position and size; the popovers keep its material, tabs and knob family.
- **Waveform**: no halo, glow band, ghost envelope or particles outside the waveform; four
  layers inside it (true peak silhouette, body, RMS energy, a spine following the energy)
  plus onset filaments from the analysis (`peakFlux`). One Shot read heads and START are
  the earlier slim 1.2 px accent lines.
- **LOOP** is two chasing circular arrows (the ring-and-stem read as a power/ON symbol).
- **Typography**: central type roles (`type::` in OspLookAndFeel.h); about 20-30 % less
  ink than before for labels and values (bold -> semibold/medium, semibold -> medium),
  metadata (file names, mode, ADSR letters) quieter still; Inter Light added for larger
  quiet labels. Measured against the reference PNG itself the old build carried about
  the same ink as the mockup; the revision follows the requested lighter direction.

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
