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
- The baseline is the tag `ui-refine-baseline`.
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
