# Visual QA

The approved reference images are the visual source of truth for the plugin's look; the
live editor is compared with them at the canonical size, **1448 x 1086**.

## Folders

- `reference/` - the approved PNGs, 1448 x 1086 (the popup as rendered over the
  instrument): `main-2-layer.png` and `popup-space.png`. These are the only approved
  references; one- and three-layer layouts and the other popups follow their language.
  `reference/earlier/` keeps the first mockups (they show controls the instrument does not
  have - Wavetable, per-card preset arrows, folder/save/settings - so they guide the visual
  language only).
- `reference/palette.md` - colours sampled from the references.
- `current/` - renders of the live editor (generated, not committed).
- `review/` - comparisons (generated, not committed).
- `final-review/` - the committed final comparison for every reference, the screenshot
  matrix and the known differences (`final-review/README.md`).

## Running it

```sh
scripts/visual-review.sh                 # render design/current/*.png and compare
python3 scripts/visual-compare.py --crop 60,460,480,120 --name source-knobs   # a detail board
```

`visual-review.sh` runs the hidden `[canonical]` plugin test (`OSP_SNAPSHOT_DIR` set) under
`xvfb-run` on Linux; it renders the reference composition (two layers, A One Shot, B
Granular, preset *Natural*, the reference's macro and envelope positions, notes playing) and
the matrix `01-empty.png` ... `21-advanced-popup.png`. Set `OSP_CANONICAL_A` / `OSP_CANONICAL_B`
to the reference's own recordings to render them instead of the stand-in sounds.

For every reference with a render of the same name, `visual-compare.py` writes
`reference.png`, `implementation.png`, `overlay-50.png` (half each: edges that jump are
wrong), `diff.png` (amplified difference), `blink.gif` and `side-by-side.png`, and prints the
mean difference. Geometry target: major edges within about 2 px, small controls 1-2 px.
