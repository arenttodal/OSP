# ANDOR OSP — final precision UI refinement

This is a visual-only pass over five interface systems. There are no DSP, parameter, workflow
or geometry changes.

The references are the specification in the conversation, plus the product owner's reference
pack (five actual screenshots and five approved direction studies, kept outside the
repository). The pack's `03_original_top_navigation.png` shows DAMPEN, and
`04_original_eq_dampen.png` shows the preset bar; the two names are swapped. The "additional
view" (`05`) is the KEY DNA editor, so it is refined in Pass 5 together with the KEY DNA
direction study.

How the images were made:
- Every image is rendered by the real editor:
  `OSP_REFINE_DIR=<dir> osp_plugin_tests "[refine-shots]"`
  (`tests/plugin/RefinementShotsTests.inc`).
- Size: 1448 × 1086, at 1× and 2×, with one, two and three layers.
- The baseline is commit `5f7dbef` (the local tag `ui-refine-baseline`).
- In each comparison image, the top half is before and the bottom half is after.

Colour rule (the owner's choice, hybrid):
- the NOTE MAP editor uses only the layer's colour;
- pitch colours stay where they carry meaning at a glance: the keyboard's light strip and the
  region of the note being played;
- the overview's tints are quieter.

## Pass 1 — NOTE MAP and its inspector

| State | Comparison |
|---|---|
| One note selected | `pass1-notemap-1L-selected.jpg` |
| Nothing selected | `pass1-notemap-1L-none.jpg` |
| Hover on another note | `pass1-notemap-1L-hover.jpg` |
| Two and three layers | `pass1-notemap-2L-selected.jpg`, `pass1-notemap-3L-selected.jpg` |
| Detail at 2× | `pass1-notemap-detail@2x.jpg` |

Changes:
- **Opaque editor.** It used to be painted at 94 % over the overview, whose region labels showed
  through behind the tab row (visible in the owner's screenshot 01). It is now the display's
  graphite, opaque.
- **One continuous waveform.**
  - There are no per-note cards, fills or pitch colours any more.
  - Note edges are fine neutral hairlines; two notes that touch share one line.
  - The waveform itself carries the state: selected 95 %, hovered 56 %, normal 42 %, between
    notes 30 %, left out 20 % (all in the layer's own wave colour).
- **One selected state.**
  - A 7 % layer-coloured field.
  - Both edges in the layer colour at 1.5 px.
  - A narrow 2 × 20 px handle on each edge, 3 px while grabbed (the hit area is unchanged).
- **Hover** raises the waveform and the edges by one step, with no glow.
- **Suggested repeat.** A finer dashed amber line with a smaller tab. It is quiet at rest and
  stronger on hover.
- **Pitch labels.** A label shows only when it fits whole. Cents are added only when they fit
  too. There is no more "G…" or "A…". The selected note's full name is in the inspector.
- **Time grid.** A label every 100 px at most (was 70), fainter guides, and no label crowding
  the left edge.
- **Hairlines.** All rules sit on the device pixel grid (`vline` / `hline`), so they are one
  sharp pixel at 1× and at 2×.
- **Top row.**
  - EQ, DAMPEN and NOTES A are one row of text tabs. The open one is bright, with a 1.5 px
    underline in the layer colour.
  - The status reads in sentence case ("Needs review"), not capitals.
  - A hairline runs under the row.
  - The undo, redo and close strokes are matched (1.3 px, round caps).
- **Inspector: no card.**
  - It shares the graphite, with one fine divider at its left.
  - The headline is the note in Inter Light (27 px; 22 when narrow), followed by quieter cents
    and take: "F5 +1 c · Take 1/2".
  - A hairline sits under the headline.
- **Values, actions and states are told apart.**
  - Values (F5, +1 c, 1) are plain text.
  - − / + are quiet glyphs that brighten under the pointer.
  - Actions (Audition, Split, Merge →, Split here / Keep one, Keep mine / Use new) are hairline
    outlines.
  - A state that is on (Included, FILL On, In tune, Auto) is filled once.
  - DETECTION is one segmented control with the chosen part filled.
  - The layer accent no longer fills every "on" cell. Labels are in sentence case.
- **"Two notes?"** now reads "Repeated?" with "Split here / Keep one".
- **Empty and processing states.** A small label at the graph's corner, no longer centred over
  the editor.

Unchanged:
- every action, increment, hit area and keyboard shortcut;
- the inspector's rows at every density;
- undo and redo.

The NOTE MAP and phase-5 tests pass (336 / 355 assertions), and the OSP–shell visual
regression stays at 0 px.

## Pass 2 — Source header

| State | Comparison |
|---|---|
| A mapped layer (NOTES, EQ, mode, menu) | `pass2-header-1L-mapped.jpg` |
| Two layers (B's EQ open) | `pass2-header-2L.jpg` |
| Three layers | `pass2-header-3L.jpg` |
| All three at 2× | `pass2-header-1-2-3-layers@2x.jpg` |

Changes:
- **Mode key.** Its drawn body is now as tall as NOTES and EQ (33 px, on their shared centre
  line), where it used to be 41 px.
  - The whole 41 px stays the hit area: `skin::KeyFace::keyArea`, a shared primitive that lets
    a key show a smaller body than its hit area.
  - The name drops to ~14.5 px at 88 % ink, the same weight as NOTES and EQ.
  - The chevron is smaller and finer (12 px, 1.25 px stroke).
- **Telltales.**
  - NOTES's "to review" is a 4.5 px flat amber dot.
  - EQ's "heard" is the same dot in the layer colour.
  - Both used to be glowing lens lamps.
  - An open editor is shown by the pressed key alone, never lit.
- **Menu.** The three dots are 4.2 px, 8.5 px apart (were 5 and 10), at 72 % ink (90 % on
  hover). It is the quietest control in the header; the 30 × 36 px hit area is unchanged.
- **Metadata truncation.** The line now degrades predictably:
  1. the whole line ("6 notes · Needs review");
  2. else its first part ("6 notes");
  3. else nothing (on a mapped layer, whose NOTES key already says it).
  It is never a word cut in two ("6 n…").

Unchanged: the card layout, every bound, the keys' materials and states, the root and badge.

## Pass 3 — Preset navigation

| State | Comparison |
|---|---|
| Preset bar, SAVE, volume and menu at 2× | `pass3-presetbar@2x.jpg` |
| The whole top row at 1× | `pass3-topbar-1x.jpg` |
| The icon family at its displayed size, magnified 4× | `pass3-icons@4x.jpg` |

Changes:
- **One icon family.**
  - Chevrons, heart, cassette and menu share a 1.5 px stroke with round caps and joins. They
    used to be drawn at 1.3–1.8 px.
  - The heart is redrawn with one symmetric path: a narrower cleft and a softer point. The
    filled heart is the same path, filled.
- **States.**
  - The chevrons rest at 60 % ink and rise to 85 % under the pointer.
  - The heart takes the accent colour when the preset is a favourite or the heart is hovered.
- **Dividers.** The two dividers in the preset bar sit on a whole pixel and keep the same
  inset from top and bottom (9 px at 1×), so they read as one sharp hairline.
- **Main menu.** The dots are 4.6 px and 9.5 px apart (were 5 and 10), at 72 % ink (90 % on
  hover): quieter than the controls beside them, matching the source header's menu.

Unchanged:
- the preset bar's size, its keys and its hit areas;
- the volume slider, its value and its gain law;
- every menu entry.

The GUI tests (128 assertions) pass, and the OSP–shell visual regression stays at 0 px.

## Pass 4 — EQ and DAMPEN

| State | Comparison |
|---|---|
| EQ, three bands in use, BELL selected | `pass4-eq-1L.jpg` |
| DAMPEN on (Shelf) | `pass4-dampen-1L.jpg` |
| Two layers | `pass4-eq-2L.jpg` |
| Both top rows at 2× | `pass4-toolbars@2x.jpg` |
| EQ off (its hint) and the pointer over a band (after only) | `pass4-eq-off-hover.jpg` |

Defects fixed:
- **Bleed-through.** The EQ was painted at 86 % over the waveform, so the waveform's time labels
  ("0.5 s", "1 s") showed in its top row. DAMPEN was painted at 92 % over the EQ page, so the
  EQ's "DAMPEN" tab ghosted behind DAMPEN's "EQ" tab ("DAMEQ"), and the EQ's hint showed through
  the curve. Both pages are now the display's opaque graphite, as NOTE MAP is. The faint
  recording behind the EQ graph goes with it (one line in `EqEditor::paint` to restore it).
- **Clipped labels.** "20k" was cut by the display's right edge ("20(") and is now right-aligned
  on its line. The scale's "0" was half hidden under DAMPEN's first point and now ends clear of it.

Changes:
- **One text-tab row on all three pages.**
  - EQ, DAMPEN and NOTES share one row style: the open page's name in 12 px with a 1.5 px
    underline in the layer's colour, the other page a quiet text tab that brightens on hover,
    and a hairline under the row.
  - The other tab sits one even gap after the name ("Off" in between while the page is off),
    as NOTE MAP's tabs are spaced.
  - The power switch, close, the band switch and the reset share one stroke (1.3–1.4 px, round
    caps).
- **Graph first.**
  - The grid sits on the device pixel grid; minor lines are fainter, decades and 0 dB stronger.
  - The EQ curve is one 1.6 px line with a 6 % fill (was 2 px with 10 %); the DAMPEN curve
    matches it.
- **One node vocabulary.** EQ bands and DAMPEN points are the same disc: 5 px, 6 px when hovered
  or selected, with a graphite gap ring. Selected adds one fine outer ring. Bands that are off
  stay hollow markers.
- **Compact readouts.**
  - The value fields (frequency, gain, Q, AMOUNT, FREQ / MIN) are text over a hairline, no
    longer boxes. Under the pointer the hairline takes the band's colour and the field fills
    faintly.
  - They still drag, double-click to type, and accept modulation drops.
- **Segmented choices.** The slope (12 | 24) and DAMPEN's mode (Shelf | Low-pass) use NOTE MAP's
  DETECTION style: one hairline frame, the chosen part filled once. Labels are in sentence case.
- **Actions.** "Curve ▾" is a hairline-outlined action that fills faintly on hover.
- **Hints.** "Drag a marker to shape this sound" and "DAMPEN is off…" are small notes at the
  graph's corner, no longer centred over the curve.

Unchanged:
- the display's bounds, the graph's area, scales and mapping;
- every drag, wheel, double-click, menu, undo step and parameter.

The `[eq]`, `[dampen]`, `[mod-final]` and `[gui]` tests pass (775 assertions), as do the per-layer
EQ tests (120). The OSP–shell visual regression stays at 0 px.

## Pass 5 — KEY DNA (the additional view)

| State | Comparison |
|---|---|
| The whole editor, SIZE shown, two keys held | `pass5-keydna.jpg` (before left, after right) |
| Header and parameter row at 2× | `pass5-keydna-header@2x.jpg` |
| Profile and curve at 2× | `pass5-keydna-graph@2x.jpg` (before left, after right) |

The hierarchy now reads, in order:
1. the parameter;
2. the curve;
3. each key's variation;
4. AMOUNT and INDIVIDUALITY;
5. the seed and the secondary actions.

Changes:
- **One parameter row.** POS SIZE DENS SPREAD TUNE SCATTER PAN are quiet text, no longer seven
  ceramic keys.
  - The shown parameter is bright with a 1.5 px underline in the layer's colour, the only
    selected mark.
  - Hover raises the ink.
  - "This parameter moves something" is a 4 px flat dot after the name, no longer a lamp.
- **AMOUNT is easy to find.**
  - It has the largest value (17 px) and the darkest caption.
  - A 2 px meter in the layer's colour runs under it.
  - It is still the host parameter, live on sounding notes and automatable, with the same
    30 ms smoothing.
- **Readouts.** AMOUNT, INDIVIDUALITY, SEED, DEPTH and VARIATION are values over a hairline, no
  longer recessed boxes. Hover fills them faintly and turns the hairline the layer's colour.
  - SEED is the quietest (13 px, 70 %).
  - "Smooth" and "Individual" are in sentence case.
- **Secondary actions.** Randomize and Reset are hairline-outlined actions (sentence case, 62 %
  ink, brighter on hover), no longer full ceramic keys. ON stays a ceramic key: it is the page's
  one physical switch.
- **Per-key variation.** Each key's offset is a thin 2 px stem from the centre line, with a tick
  exactly at its value (as wide as the key allows), no longer a block bar.
  - A held key's stem and tick are bright, replacing the white dot above the bar.
  - The overview's seven rows use the same fine device-pixel ticks.
- **The curve.** One 1.6 px line (was 2 px). Its points use the EQ's node (5 px, 6 px while
  dragged, graphite gap ring).
- **Grid and hints.** The grid sits on the device pixel grid. "KEY DNA is off…" and "Draw a
  curve or raise VARIATION…" are small notes at the graph's corner.
- **The KEY DNA key on the granular strip.** A hairline cell with a flat 4.5 px telltale (NOTES
  and EQ's), no longer a lamp on a filled key.

Unchanged:
- the window, the graph's area and the key mapping (still the real keyboard's key centres);
- every hit area, drag, double-click, keyboard step and undo step;
- the deterministic profile.

The `[keydna]`, `[phase5]` and `[gui]` tests pass (396 assertions), and the OSP–shell visual
regression stays at 0 px.

## Shared primitives

These are the same few drawing rules everywhere the display language appears (NOTE MAP, EQ,
DAMPEN, KEY DNA), so the pages read as one instrument:

| Primitive | Where | What it guarantees |
|---|---|---|
| `vline` / `hline` | NoteMapEditor, EqEditor, KeyDnaEditor | Rules on the device pixel grid: one sharp pixel at 1× and 2× |
| Text tabs | NOTE MAP, EQ, DAMPEN, KEY DNA parameters | Quiet ink; the open one bright with a 1.5 px underline in the layer's colour; one hairline under the row |
| Readout | EQ / DAMPEN value fields, KEY DNA fields | Value over a hairline; hover fills faintly and colours the hairline |
| Segmented choice | DETECTION, 12 \| 24, Shelf \| Low-pass | One hairline frame, the chosen part filled once |
| Action cell | NOTE MAP actions, Curve ▾, Randomize, Reset, KEY DNA key | Hairline outline, faint fill on hover |
| Node | EQ bands, DAMPEN points, KEY DNA points | 5 px disc (6 px hot or selected), graphite gap ring; selected adds one outer ring |
| Telltale | NOTES, EQ, KEY DNA keys; KEY DNA parameters | A flat 4–4.5 px dot in the state's colour, never a lamp |
| Corner note | Hints and states on every graph | Small, top-left, never over the data |
| `skin::KeyFace::keyArea` | Mode key | A key may show a smaller body than its hit area |
| Icon strokes | Preset bar, menus, close, undo / redo, power | 1.3 px on the displays, 1.5 px on the housing, round caps |

Radius vocabulary: 3–4 px for small display controls, 5–7 px for housing keys (unchanged), 9–10
px for the display wells (unchanged).

## Regression results

| Check | Result |
|---|---|
| OSP–shell visual regression (`osp_visual_reference`, `andor_example_tests_visual`) | 0 px, after every pass |
| NOTE MAP and phase 5 | pass |
| `[eq]`, `[dampen]`, `[mod-final]`, `[gui]`, per-layer EQ | pass (775 + 120 assertions) |
| `[keydna]`, `[phase5]`, `[gui]` | pass (396 assertions) |
| Full `ctest`, both trees | see the final commit message |
| Design bible (`andor-design-system/guide/img`) | regenerated with `[design-capture]`; only images whose pixels changed were replaced |

No DSP, parameter, preset, schema or golden file changed in any pass.

## Remaining issues

- **The EQ graph no longer shows the recording faintly behind it.** This was the price of
  ending the label bleed-through. If the owner misses it, `EqEditor::paint` can paint the well at
  86 % again, below a separately opaque top row.
- **The KEY DNA curve looks stepped** where it crosses the keyboard's octave gaps. It is drawn
  exactly as compiled, through the real key centres, so the steps are accurate. A smoother
  drawing would misplace it relative to the keys.
- **The curve's end points sit at the graph's edges, beyond the keyboard's last keys** (A0 and
  C8). This is unchanged; they are the curve's 0 and 127.
- **Font rendering.** The headless renders use JUCE's software renderer. On macOS the CoreGraphics
  text is slightly heavier, so check hairline weights there once before release.

## How to review in the plugin

1. Build the plugin: `cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin`
   (or take the macOS CI artifact for this branch). Open the Standalone, or the AU / VST3 in a
   DAW.
2. **Preset navigation.** Hover the arrows and the heart, toggle a favourite, open the main menu
   and SAVE.
3. **Source header.** Drop a recording on A and choose "Map notes" in the prompt. NOTES, EQ, the
   mode key and the menu now share one height and weight.
4. **NOTE MAP.** Open NOTES and select, hover, split and merge notes. The inspector's values,
   actions and states should read differently at a glance.
5. **EQ and DAMPEN.** Open EQ, drag a band, select it and read its values, then switch to DAMPEN
   with the text tab and back. Turn each page off and on ("Off" after the name).
6. **KEY DNA.** Switch a layer to Granular and open KEY DNA. Step through the parameters, drag the
   curve, and play keys to see their stems light. Drag AMOUNT and automate it from the DAW.
7. To render the review images yourself:
   `OSP_REFINE_DIR=<dir> build-plugin/apps/plugin/osp_plugin_tests "[refine-shots]"`.
   To compare with before, copy `tests/plugin/RefinementShotsTests.inc` (and its include line) into a worktree of commit `5f7dbef` and render the same set there.
